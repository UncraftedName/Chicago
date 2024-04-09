#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <PathCch.h>
#include <TlHelp32.h>
#include <Psapi.h>

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "ch_recv.h"

#pragma comment(lib, "Pathcch.lib")

#define MAX_SELECT_GAMES 9

typedef struct ch_recv_ctx {
    ch_log_level log_level;
    HANDLE pipe;
    HANDLE game;
    HANDLE wait_event;
    LPVOID remote_thread_alloc;
    const ch_datamap_save_info* save_info;
} ch_recv_ctx;

// print the fmt followed by the winapi_error
void ch_log_sys_err(const ch_recv_ctx* ctx, DWORD winapi_error, const char* fmt, ...)
{
    if (ctx->log_level > CH_LL_ERROR)
        return;
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    CH_LOG_ERROR(ctx, " (GLE=%lu)", winapi_error);
    char err_msg[1024];
    DWORD err_msg_len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                       NULL,
                                       winapi_error,
                                       MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                                       err_msg,
                                       sizeof err_msg,
                                       NULL);

    if (err_msg_len == 0)
        CH_LOG_ERROR(ctx, " FormatMessageA failed: (GLE=%lu)", GetLastError());
    else
        CH_LOG_ERROR(ctx, " %s", err_msg);
}

// setup ctx->pipe
BOOL ch_create_pipe(ch_recv_ctx* ctx)
{
    ctx->pipe = CreateNamedPipeA(CH_PIPE_NAME,
                                 PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                                 PIPE_WAIT | PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
                                 1,
                                 0,
                                 CH_PIPE_INIT_BUF_SIZE,
                                 CH_PIPE_TIMEOUT_MS,
                                 NULL);

    if (ctx->pipe == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) {
            CH_LOG_ERROR(ctx,
                         "CreateNamedPipe failed, make sure you're not running another instance of this application.");
        } else {
            ch_log_sys_err(ctx, err, "CreateNamedPipe failed:");
        }
    }
    return ctx->pipe != INVALID_HANDLE_VALUE;
}

// setup ctx->game by finding a game that we want to inject the payload into
BOOL ch_find_game(ch_recv_ctx* ctx)
{
    DWORD proc_ids[MAX_SELECT_GAMES];
    HANDLE proc_handles[MAX_SELECT_GAMES] = {0};
    int num_found_games = 0;

    // 1) iterate all processes and find those which might be source games

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        ch_log_sys_err(ctx, GetLastError(), "CreateToolhelp32Snapshot failed:");
        return FALSE;
    }

    PROCESSENTRY32W pe32w = {.dwSize = sizeof(PROCESSENTRY32W)};
    if (Process32FirstW(snap, &pe32w)) {
        do {
            if (ch_get_required_modules(pe32w.th32ProcessID, NULL)) {
                proc_ids[num_found_games] = pe32w.th32ProcessID;
                if (++num_found_games >= MAX_SELECT_GAMES) {
                    CH_LOG_ERROR(ctx,
                                 "Found more than %d candidate source engine games, close some and try again.",
                                 MAX_SELECT_GAMES);
                    CloseHandle(snap);
                    return FALSE;
                }
            }
        } while (Process32NextW(snap, &pe32w));
    }
    CloseHandle(snap);

    if (num_found_games == 0) {
        CH_LOG_ERROR(ctx, "No candidate source engine games found, launch a source game and try again.");
        return FALSE;
    }

    // 2) we found some candidate processes, print their info

    num_found_games = min(num_found_games, MAX_SELECT_GAMES);

    char msg[(MAX_PATH + 64) * MAX_SELECT_GAMES + 64];
    size_t msg_off = 0;
    if (num_found_games > 1)
        msg_off += snprintf(msg, sizeof(msg), "Multiple candidate source games found:\n");

    const char* failed_func = NULL;
    for (int i = 0; i < num_found_games; i++) {
        proc_handles[i] = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_WRITE | PROCESS_VM_READ |
                                          PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
                                      false,
                                      proc_ids[i]);
        if (!proc_handles[i]) {
            failed_func = "OpenProcess";
            break;
        }
        char game_path[MAX_PATH];
        DWORD len = GetModuleFileNameExA(proc_handles[i], NULL, game_path, MAX_PATH);
        if (len == 0) {
            failed_func = "GetModuleFileNameEx";
            break;
        }

        if (num_found_games == 1) {
            msg_off += snprintf(msg, sizeof(msg) - msg_off, "(PID: %lu) '%s'", proc_ids[i], game_path);
        } else {
            msg_off +=
                snprintf(msg, sizeof(msg) - msg_off, " [%d]: (PID: %05lu) '%s'\n", i + 1, proc_ids[i], game_path);
        }
    }

    if (failed_func) {
        // some function failed, cleanup all handles and return
        DWORD err = GetLastError();
        for (int i = 0; i < num_found_games; i++)
            if (proc_handles[i])
                CloseHandle(proc_handles[i]);
        ch_log_sys_err(ctx, err, "%s failed:", failed_func);
        return FALSE;
    }

    msg[sizeof(msg) - 1] = '\0';

    // in either case, keep the handle to the game process open, we'll need it to inject the payload
    if (num_found_games == 1) {
        // exactly one candidate game was found, choose it
        CH_LOG_INFO(ctx, "Choosing source game: %s\n", msg);
        ctx->game = proc_handles[0];
    } else {
        // we found multiple candidate games, print them and let the user decide which one to use
        printf("%s", msg);
        int select;
        for (;;) {
            printf("Select a source game by number (0-%d): ", MAX_SELECT_GAMES);
            int ret = scanf("%d", &select);
            if (ret != 1 && select > 0 && select <= MAX_SELECT_GAMES)
                break;
        }
        for (int i = 0; i < num_found_games; i++)
            if (i != select)
                CloseHandle(proc_handles[i]);
        ctx->game = proc_handles[select];
    }
    return TRUE;
}

// setup ctx->remote_thread_alloc, inject the payload, & start its thread
BOOL ch_inject(ch_recv_ctx* ctx)
{
    // TODO unload the payload if it's already in the game here
    // TODO check if the dll exists on disk (ideally we should check that before finding a game, or maybe we could just try loading and not worry about it)

    wchar_t payload_path[MAX_PATH];

    DWORD file_name_res = GetModuleFileNameW(NULL, payload_path, MAX_PATH);
    if (file_name_res == 0 || file_name_res == MAX_PATH) {
        ch_log_sys_err(ctx, GetLastError(), "GetModuleFileName failed:");
        return FALSE;
    }

    // TODO this is hardcoded for now, any way to move it to a #define?
    HRESULT path_cch_res = PathCchRemoveFileSpec(payload_path, MAX_PATH);
    if (path_cch_res == S_OK)
        path_cch_res = PathCchCombine(payload_path, MAX_PATH, payload_path, L"chicago_payload.dll");
    if (path_cch_res != S_OK) {
        CH_LOG_ERROR(ctx, "A PathCch function failed.");
        return FALSE;
    }

    ctx->remote_thread_alloc = VirtualAllocEx(ctx->game, NULL, sizeof payload_path, MEM_COMMIT, PAGE_READWRITE);
    if (!ctx->remote_thread_alloc) {
        ch_log_sys_err(ctx,
                       GetLastError(),
                       "Failed to inject payload, reopen the game and try again. VirtualAllocEx failed:");
        return FALSE;
    }

    BOOL write_res = WriteProcessMemory(ctx->game, ctx->remote_thread_alloc, payload_path, sizeof payload_path, NULL);
    if (!write_res) {
        ch_log_sys_err(ctx, GetLastError(), "WriteProcessMemory failed:");
        return FALSE;
    }

    LPTHREAD_START_ROUTINE thread_start = (LPTHREAD_START_ROUTINE)(void*)LoadLibraryW;
    HANDLE payload_thread = CreateRemoteThread(ctx->game, NULL, 0, thread_start, ctx->remote_thread_alloc, 0, NULL);
    if (!payload_thread) {
        ch_log_sys_err(ctx, GetLastError(), "CreateRemoteThread failed:");
        return FALSE;
    }

    /*
    * Closing the handle here doesn't stop the payload thread, and in fact we wouldn't want to do that ourselves.
    * The payload immediately creates a new main thread, and even if we could, calling TerminateThread on it is
    * a terrible idea. If we happen to exit prematurely, the payload thread will notice that its connection got
    * dropped and exit cleanly on its own.
    */
    CloseHandle(payload_thread);
    return TRUE;
}

// setup ctx->wait_event and wait for the payload to connect to us (with a timeout)
BOOL ch_await_connect(ch_recv_ctx* ctx)
{
    ctx->wait_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ctx->wait_event) {
        ch_log_sys_err(ctx, GetLastError(), "CreateEvent failed:");
        return FALSE;
    }

    OVERLAPPED overlapped = {.hEvent = ctx->wait_event};

    // overlapped (async) connect
    BOOL connect_res = ConnectNamedPipe(ctx->pipe, &overlapped);
    if (connect_res)
        assert(0); // overlapped connect should always return 0

    DWORD err = GetLastError();
    switch (err) {
        case ERROR_PIPE_CONNECTED:
            return TRUE;
        case ERROR_IO_PENDING:
            break;
        default:
            ch_log_sys_err(ctx, err, "ConnectNamedPipe failed:");
            return FALSE;
    }

    /*
    * Wait for a connection. This is pretty much the only operation I'm genuinely worried about failing,
    * since after this point we can tell what the state of the payload is short of it getting stuck in
    * some infinite loop. I'd like to have a timeout here with a pretty error message instead of just
    * hanging forever. From reading the docs & SO the only way I see of doing that is by making the
    * whole pipe handle overlapped (async) just to be able to add a timeout to this one wait call. Very
    * silly and it adds a bit of complexity to the whole recv side of the pipe, oh well.
    */
    DWORD wait_res = WaitForSingleObject(ctx->wait_event, CH_PIPE_TIMEOUT_MS);
    switch (wait_res) {
        case WAIT_FAILED:
            ch_log_sys_err(ctx, GetLastError(), "WaitForSingleObject failed:");
            return FALSE;
        case WAIT_TIMEOUT:
            CH_LOG_ERROR(ctx, "Timed out waiting for payload (WaitForSingleObject).");
            return FALSE;
        default:
            break;
    }

    DWORD _;
    connect_res = GetOverlappedResult(ctx->pipe, &overlapped, &_, FALSE);
    if (!connect_res) {
        ch_log_sys_err(ctx, GetLastError(), "GetOverlappedResult (ConnectNamedPipe) failed:");
        return FALSE;
    }
    return TRUE;
}

// client has connected, process requests
BOOL ch_recv_loop(ch_recv_ctx* ctx)
{
    BOOL have_read_result = FALSE;
    BOOL read_success = FALSE;
    DWORD read_n_bytes = 0;
    DWORD n_loops_without_success = 0;
    OVERLAPPED overlapped = {.hEvent = ctx->wait_event};

    enum ch_recv_state {
        CH_RS_RUNNING,
        CH_RS_ERROR,
        CH_RS_DONE,
    } state = CH_RS_RUNNING;

    struct ch_process_msg_ctx* process_ctx = ch_msg_ctx_alloc(ctx->log_level, CH_PIPE_INIT_BUF_SIZE, ctx->save_info);

    if (!process_ctx) {
        CH_LOG_ERROR(ctx, "Out of memory (ch_recv_loop malloc).");
        state = CH_RS_ERROR;
    }

    /*
    * This loop is a bit wacky and I'm honestly still lost in the sauce about what exactly
    * GetOverlappedResult does. I *think* it returns what ReadFile *would* return if it were
    * synchronous, but only once the async ReadFile has completed. So we try the read, and
    * if it is pending then we wait for it to finish with WaitForSingleObject, then check
    * GetOverlappedResult.
    * 
    * In case the buffer needs to be expanded, we will do multiple calls to read & each one
    * will continue reading where the previous one left off. I'm *pretty sure* that it's the
    * overlapped structure that keeps track of where to read next, but we don't need to reset
    * the structure in order to start reading at the beginning of the next message (I couldn't
    * find any mention of this in the docs). Also, ReadFile and GetOverlappedResult functions
    * will reset the wait_event to non-signalled so we don't have to worry about that.
    */
    while (state == CH_RS_RUNNING) {

        if (!have_read_result)
            read_success = ReadFile(ctx->pipe,
                                    ch_msg_ctx_buf(process_ctx),
                                    ch_msg_ctx_buf_capacity(process_ctx),
                                    &read_n_bytes,
                                    &overlapped);
        have_read_result = FALSE;

        if (read_success) {
            n_loops_without_success = 0;
            ch_msg_ctx_buf_consumed(process_ctx, read_n_bytes);
            if (!ch_msg_ctx_process(process_ctx))
                state = CH_RS_DONE;
            continue;
        } else if (n_loops_without_success++ > 10) {
            CH_LOG_ERROR(ctx, "recv loop has failed too many times, something fishy is going on");
            state = CH_RS_ERROR;
            break;
        }

        DWORD err = GetLastError();
        switch (err) {
            case ERROR_IO_PENDING:
                // not an error - wait for async read to complete, no timeout necessary
                DWORD wait_result = WaitForSingleObject(ctx->wait_event, INFINITE);
                if (wait_result == WAIT_OBJECT_0) {
                    read_success = GetOverlappedResult(ctx->pipe, &overlapped, &read_n_bytes, FALSE);
                    have_read_result = TRUE;
                } else {
                    ch_log_sys_err(ctx, GetLastError(), "WaitForSingleObject failed:");
                    state = CH_RS_ERROR;
                }
                break;
            case ERROR_MORE_DATA:
                /*
                * Not an error - the message is just bigger than our buffer. Note that
                * read_n_bytes is 0 for some reason even though we did read bytes, so
                * increment the msg_len by however much we specified in the read call.
                */
                ch_msg_ctx_buf_consumed(process_ctx, ch_msg_ctx_buf_capacity(process_ctx));
                if (!ch_msg_ctx_buf_expand(process_ctx)) {
                    CH_LOG_ERROR(ctx, "Out of memory (ch_recv_loop realloc).");
                    state = CH_RS_ERROR;
                }
                break;
            case ERROR_BROKEN_PIPE:
                CH_LOG_ERROR(ctx, "Payload disconnected before sending goodbye message.");
                state = CH_RS_ERROR;
                break;
            default:
                ch_log_sys_err(ctx, GetLastError(), "ReadFile (or GetOverlappedResult) failed:");
                state = CH_RS_ERROR;
                break;
        }
    }
    ch_msg_ctx_free(process_ctx);
    assert(state == CH_RS_ERROR || state == CH_RS_DONE);
    return state == CH_RS_DONE;
}

#define CH_RUN_IF_OK(x) \
    {                   \
        if (ok)         \
            ok = x;     \
    }

void ch_do_inject_and_recv_maps(const ch_datamap_save_info* save_info, ch_log_level log_level)
{
    ch_recv_ctx rctx = {
        .log_level = log_level,
        .pipe = INVALID_HANDLE_VALUE,
        .game = INVALID_HANDLE_VALUE,
        .wait_event = NULL,
        .remote_thread_alloc = NULL,
        .save_info = save_info,
    };
    ch_recv_ctx* ctx = &rctx;

    BOOL ok = TRUE;

    CH_RUN_IF_OK(ch_create_pipe(ctx));
    CH_RUN_IF_OK(ch_find_game(ctx));
    CH_RUN_IF_OK(ch_inject(ctx));

    /*
    * In case connecting fails, if we try to cleanup ctx we might end up deallocating memory
    * that is currently being read by LoadLibrary, which would crash the game. I would like
    * to avoid that. However, we don't actually know that LoadLibrary is done with that
    * memory until the payload connects to us. If an error happens before then, avoid
    * freeing that memory. This can cause a small memory leak in the game, boohoo.
    */
    LPVOID tmp_alloc = ctx->remote_thread_alloc;
    ctx->remote_thread_alloc = NULL;
    CH_RUN_IF_OK(ch_await_connect(ctx));
    CH_RUN_IF_OK(TRUE; ctx->remote_thread_alloc = tmp_alloc);

    CH_RUN_IF_OK(ch_recv_loop(ctx));

    // TODO: we can destroy all the pipe stuff in ctx, now we should verify that the messages make sense

    // destroy everything in ctx

    if (ctx->game != INVALID_HANDLE_VALUE) {
        if (ctx->remote_thread_alloc)
            VirtualFreeEx(ctx->game, ctx->remote_thread_alloc, 0, MEM_RELEASE);
        CloseHandle(ctx->game);
    }
    if (ctx->pipe != INVALID_HANDLE_VALUE)
        CloseHandle(ctx->pipe);
    if (ctx->wait_event)
        CloseHandle(ctx->wait_event);
}
