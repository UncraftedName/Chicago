#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <PathCch.h>
#include <TlHelp32.h>
#include <Psapi.h>

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "ch_inject.h"
#include "ch_msgpack.h"

#pragma comment(lib, "Pathcch.lib")

#define MAX_SELECT_GAMES 9

typedef enum ch_exit_code {
    CH_EC_OK = 0,
    CH_EC_OUT_OF_MEMORY,
    CH_EC_CREATE_PIPE_FAILED,
    CH_EC_FIND_GAME_FAILED,
    CH_EC_INJECT_FAILED,
    CH_EC_CONNECT_FAILED,
} ch_exit_code;

typedef struct ch_recv_ctx {
    ch_exit_code next_exit_code;
    ch_comm_msg_type log_level;
    msgpack_zone mp_zone;
    HANDLE pipe;
    HANDLE game;
    HANDLE wait_event;
    LPVOID remote_thread_alloc;
} ch_recv_ctx;

#define CH_LOG(ctx, level, ...)                                        \
    {                                                                  \
        assert(level == CH_MSG_LOG_INFO || level == CH_MSG_LOG_ERROR); \
        if ((level) >= (ctx).log_level)                                \
            fprintf(stderr, __VA_ARGS__);                              \
    }

#define CH_VLOG(ctx, level, fmt, va)                                   \
    {                                                                  \
        assert(level == CH_MSG_LOG_INFO || level == CH_MSG_LOG_ERROR); \
        if ((level) >= (ctx).log_level)                                \
            vfprintf(stderr, fmt, va);                                 \
    }

void ch_free_ctx(ch_recv_ctx* ctx)
{
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

// TODO now that we've broken up the whole process into several functions it should be much easier to just return early instead of exiting the whole thread
void ch_err_and_exit(ch_recv_ctx* ctx, DWORD winapi_error, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    CH_VLOG(*ctx, CH_MSG_LOG_ERROR, fmt, va);
    va_end(va);
    if (winapi_error != ERROR_SUCCESS && ctx->next_exit_code != CH_EC_OK) {
        CH_LOG(*ctx, CH_MSG_LOG_ERROR, " (GLE=%lu) ", winapi_error);
        char err_msg[1024];
        DWORD err_msg_len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                           NULL,
                                           winapi_error,
                                           MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                                           err_msg,
                                           sizeof err_msg,
                                           NULL);

        if (err_msg_len == 0) {
            CH_LOG(*ctx, CH_MSG_LOG_ERROR, "FormatMessageA failed: (GLE=%lu)", GetLastError());
        } else {
            CH_LOG(*ctx, CH_MSG_LOG_ERROR, "%s", err_msg);
        }
    }
    ch_free_ctx(ctx);
    ExitThread(ctx->next_exit_code);
}

void ch_create_pipe(ch_recv_ctx* ctx)
{
    ctx->next_exit_code = CH_EC_CREATE_PIPE_FAILED;

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
        if (err == ERROR_PIPE_BUSY)
            ch_err_and_exit(
                ctx,
                CH_EC_OK,
                "CreateNamedPipe failed, make sure you're not running another instance of this application.");
        else
            ch_err_and_exit(ctx, err, "CreateNamedPipe failed:");
    }
}

void ch_find_game(ch_recv_ctx* ctx)
{
    ctx->next_exit_code = CH_EC_FIND_GAME_FAILED;

    DWORD candidate_proc_ids[MAX_SELECT_GAMES];
    HANDLE candidate_proc_handles[MAX_SELECT_GAMES] = {0};
    int num_found_games = 0;

    ch_find_candidate_games(candidate_proc_ids, MAX_SELECT_GAMES, &num_found_games);
    if (num_found_games == 0)
        ch_err_and_exit(ctx,
                        ERROR_SUCCESS,
                        "No candidate source engine games found, launch a source game and try again.");
    if (num_found_games > MAX_SELECT_GAMES)
        ch_err_and_exit(ctx,
                        ERROR_SUCCESS,
                        "Found more than %d candidate source games, close some and try again.",
                        MAX_SELECT_GAMES);

    num_found_games = min(num_found_games, MAX_SELECT_GAMES);

    char msg[(MAX_PATH + 64) * MAX_SELECT_GAMES + 64];
    size_t msg_off = 0;
    if (num_found_games > 1)
        msg_off += snprintf(msg, sizeof(msg), "Multiple candidate source games found:\n");

    const char* failed_func = NULL;
    for (int i = 0; i < num_found_games; i++) {
        candidate_proc_handles[i] = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_WRITE | PROCESS_VM_READ |
                                                    PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
                                                false,
                                                candidate_proc_ids[i]);
        if (!candidate_proc_handles[i]) {
            failed_func = "OpenProcess";
            break;
        }
        char game_path[MAX_PATH];
        DWORD len = GetModuleFileNameExA(candidate_proc_handles[i], NULL, game_path, MAX_PATH);
        if (len == 0) {
            failed_func = "GetModuleFileNameEx";
            break;
        }
        if (num_found_games == 1)
            msg_off += snprintf(msg, sizeof(msg) - msg_off, "(PID: %lu) '%s'", candidate_proc_ids[i], game_path);
        else
            msg_off += snprintf(msg,
                                sizeof(msg) - msg_off,
                                " [%d]: (PID: %05lu) '%s'\n",
                                i + 1,
                                candidate_proc_ids[i],
                                game_path);
    }

    if (failed_func) {
        DWORD err = GetLastError();
        for (int i = 0; i < num_found_games; i++)
            if (candidate_proc_handles[i])
                CloseHandle(candidate_proc_handles[i]);
        ch_err_and_exit(ctx, err, "%s failed:", failed_func);
    }

    msg[sizeof(msg) - 1] = '\0';
    if (num_found_games == 1) {
        CH_LOG(*ctx, CH_MSG_LOG_INFO, "Choosing source game: %s\n", msg);
        ctx->game = candidate_proc_handles[0];
    } else {
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
                CloseHandle(candidate_proc_handles[i]);
        ctx->game = candidate_proc_handles[select];
    }
}

void ch_inject(ch_recv_ctx* ctx)
{
    ctx->next_exit_code = CH_EC_INJECT_FAILED;

    // TODO unload the payload if it's already in the game here
    // TODO check if the dll exists on disk (ideally we should check that before finding a game, or maybe we could just try loading and not worry about it)

    wchar_t payload_path[MAX_PATH];
    ctx->remote_thread_alloc = VirtualAllocEx(ctx->game, NULL, sizeof payload_path, MEM_COMMIT, PAGE_READWRITE);
    if (!ctx->remote_thread_alloc)
        ch_err_and_exit(ctx,
                        GetLastError(),
                        "Failed to inject payload, reopen the game and try again. VirtualAllocEx failed:");

    DWORD file_name_res = GetModuleFileNameW(NULL, payload_path, MAX_PATH);
    if (file_name_res == 0 || file_name_res == MAX_PATH)
        ch_err_and_exit(ctx, GetLastError(), "GetModuleFileName failed:");

    // TODO this is hardcoded for now, any way to move it to a #define?
    HRESULT path_cch_res = PathCchRemoveFileSpec(payload_path, MAX_PATH);
    if (path_cch_res == S_OK)
        path_cch_res = PathCchCombine(payload_path, MAX_PATH, payload_path, L"chicago_payload.dll");
    if (path_cch_res != S_OK)
        ch_err_and_exit(ctx, ERROR_SUCCESS, "A PathCch function failed.");

    BOOL write_res = WriteProcessMemory(ctx->game, ctx->remote_thread_alloc, payload_path, sizeof payload_path, NULL);
    if (!write_res)
        ch_err_and_exit(ctx, GetLastError(), "WriteProcessMemory failed:");
    LPTHREAD_START_ROUTINE start = (LPTHREAD_START_ROUTINE)(void*)LoadLibraryW;
    HANDLE payload_thread = CreateRemoteThread(ctx->game, NULL, 0, start, ctx->remote_thread_alloc, 0, NULL);
    if (!payload_thread)
        ch_err_and_exit(ctx, GetLastError(), "CreateRemoteThread failed:");
    // the payload immediately creates a new thread, and even if we could TerminateThread that it would be a very bad idea
    CloseHandle(payload_thread);
}

void ch_await_connect(ch_recv_ctx* ctx)
{
    ctx->next_exit_code = CH_EC_CONNECT_FAILED;

    ctx->wait_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ctx->wait_event)
        ch_err_and_exit(ctx, GetLastError(), "CreateEvent failed:");

    OVERLAPPED overlapped = {.hEvent = ctx->wait_event};

    // overlapped (async) connect
    BOOL connect_res = ConnectNamedPipe(ctx->pipe, &overlapped);
    if (connect_res) {
        // overlapped connect should always return 0
        assert(0);
    }

    DWORD err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED)
        return;
    else if (err != ERROR_IO_PENDING)
        ch_err_and_exit(ctx, err, "ConnectNamedPipe failed:");

    /*
    * Wait for a connection. This is pretty much the only operation I'm genuinely worried about failing,
    * since after this point we can tell what the state of the payload is short of it getting stuck in
    * some infinite loop. I'd like to have a timeout here with a pretty error message instead of just
    * hanging forever. From reading the docs & SO the only way I see of doing that is by making the
    * whole pipe handle overlapped (async) just to be able to add a timeout to this one wait call. Very
    * silly and it adds a bit of complexity to the whole recv side of the pipe, oh well.
    */
    DWORD wait_res = WaitForSingleObject(ctx->wait_event, CH_PIPE_TIMEOUT_MS);
    if (wait_res == WAIT_FAILED)
        ch_err_and_exit(ctx, GetLastError(), "WaitForSingleObject failed:");
    else if (wait_res == WAIT_TIMEOUT)
        ch_err_and_exit(ctx, ERROR_SUCCESS, "Timed out waiting for payload (WaitForSingleObject).");

    DWORD _;
    connect_res = GetOverlappedResult(ctx->pipe, &overlapped, &_, FALSE);
    if (!connect_res)
        ch_err_and_exit(ctx, GetLastError(), "GetOverlappedResult (ConnectNamedPipe) failed:");
}

// return true if we're expecting more messages
bool ch_process_msg(ch_recv_ctx* ctx, const char* buf, size_t buf_size)
{
    msgpack_object o;
    msgpack_unpack(buf, buf_size, NULL, &ctx->mp_zone, &o);
    msgpack_object_print(stdout, o);
    printf("\n");
    return true;
}

// client has connected, process requests
void ch_recv_loop(ch_recv_ctx* ctx)
{
    char* buf = malloc(CH_PIPE_INIT_BUF_SIZE);
    if (!buf)
        ch_err_and_exit(ctx, ERROR_SUCCESS, "Out of memory (ch_recv_loop malloc).");
    size_t buf_size = CH_PIPE_INIT_BUF_SIZE;

    size_t msg_len = 0;
    BOOL try_read = TRUE;
    BOOL read_success = FALSE;
    DWORD bytes_recvd = 0;
    OVERLAPPED overlapped = {.hEvent = ctx->wait_event};

    for (;;) {
        if (try_read)
            read_success = ReadFile(ctx->pipe, buf + msg_len, buf_size - msg_len, &bytes_recvd, &overlapped);
        try_read = TRUE;
        if (read_success) {
            msg_len += bytes_recvd;
            if (!ch_process_msg(ctx, buf, msg_len))
                break;
            msg_len = 0;
            continue;
        }
        DWORD err = GetLastError();
        switch (err) {
            case ERROR_MORE_DATA:
                // bytes_recvd is 0 in this case even though we receive bytes, so use buf_size instead
                msg_len += buf_size;
                char* new_buf = realloc(buf, buf_size * 2);
                if (!new_buf)
                    ch_err_and_exit(ctx, ERROR_SUCCESS, "Out of memory (ch_recv_loop realloc).");
                buf = new_buf;
                buf_size *= 2;
                break;
            case ERROR_BROKEN_PIPE:
                ch_err_and_exit(ctx, ERROR_SUCCESS, "Payload disconnected before sending goodbye message.");
                // TODO yeah don't return, when changing all of the errors to be returns instead of exit threads make sure to hit the free at the end of this function
                return;
            case ERROR_IO_PENDING:
                DWORD wait_result = WaitForSingleObject(ctx->wait_event, INFINITE);
                if (wait_result != WAIT_OBJECT_0)
                    ch_err_and_exit(ctx, GetLastError(), "WaitForSingleObject failed");
                read_success = GetOverlappedResult(ctx->pipe, &overlapped, &bytes_recvd, FALSE);
                try_read = FALSE;
                break;
            default:
                ch_err_and_exit(ctx, GetLastError(), "ReadFile (or GetOverlappedResult) failed:");
                return;
        }
    }
    free(buf);
}

void ch_do_inject_and_recv_maps(ch_comm_msg_type log_level)
{
    assert(log_level == CH_MSG_LOG_INFO || log_level == CH_MSG_LOG_ERROR);

    ch_recv_ctx ctx = {
        .next_exit_code = CH_EC_OUT_OF_MEMORY,
        .log_level = log_level,
        .pipe = INVALID_HANDLE_VALUE,
        .game = INVALID_HANDLE_VALUE,
        .wait_event = NULL,
        .remote_thread_alloc = NULL,
    };

    if (!msgpack_zone_init(&ctx.mp_zone, CH_PIPE_INIT_BUF_SIZE / sizeof(void*)))
        ch_err_and_exit(&ctx, ERROR_SUCCESS, "Out of memory (ch_recv_ctx init).");

    ch_create_pipe(&ctx);
    ch_find_game(&ctx);
    ch_inject(&ctx);

    // avoid freeing memory that's being read by LoadLibrary until we're sure it's not being used anymore, may result in a tiny mem leak
    LPVOID tmp_alloc = ctx.remote_thread_alloc;
    ctx.remote_thread_alloc = NULL;
    ch_await_connect(&ctx);
    ctx.remote_thread_alloc = tmp_alloc;

    ch_recv_loop(&ctx);

    ch_free_ctx(&ctx);
    CH_LOG(ctx, CH_MSG_LOG_INFO, "Done.\n");
}

void ch_find_candidate_games(DWORD* proc_ids, int num_entries, int* num_entries_returned)
{
    *num_entries_returned = 0;
    if (!proc_ids || num_entries <= 0)
        return;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W pe32w = {.dwSize = sizeof(PROCESSENTRY32W)};
    if (Process32FirstW(snap, &pe32w)) {
        do {
            if (ch_get_required_modules(pe32w.th32ProcessID, NULL)) {
                proc_ids[*num_entries_returned] = pe32w.th32ProcessID;
                if (++*num_entries_returned >= num_entries)
                    break;
            }
        } while (Process32NextW(snap, &pe32w));
    }
    CloseHandle(snap);
}
