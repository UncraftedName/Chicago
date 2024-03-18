#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <libloaderapi.h>

#include <stdio.h>

#include "ch_search.h"

typedef struct ch_send_ctx {
    HANDLE module_;
    HANDLE pipe;
} ch_send_ctx;

static void ch_clean_exit(ch_send_ctx* ctx, ch_payload_exit_code exit_code)
{
    if (ctx->pipe != INVALID_HANDLE_VALUE)
        CloseHandle(ctx->pipe);
    FreeLibraryAndExitThread(ctx->module_, exit_code);
}

static void ch_connect_client_pipe(ch_send_ctx* ctx)
{
    if (!WaitNamedPipeA(CH_PIPE_NAME, CH_PIPE_TIMEOUT_MS))
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_PIPE_WAIT_FAIL);
    ctx->pipe = CreateFileA(CH_PIPE_NAME, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD err = GetLastError();
    if (ctx->pipe == INVALID_HANDLE_VALUE)
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_PIPE_CONNECT_FAIL);
}

static void ch_send_msg(ch_send_ctx* ctx, LPCVOID data, DWORD n_bytes)
{
    BOOL success = WriteFile(ctx->pipe, data, n_bytes, NULL, NULL);
    if (!success)
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_PIPE_SEND_FAIL);
}

/*static void ch_send_log_msg(ch_send_ctx* ctx, ch_comm_msg_type level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    buf[0] = (char)level;
    int len = vsnprintf(buf + 1, sizeof(buf) - 1, fmt, args);
    ch_send_msg(ctx, buf, len + 2);
    if (level == CH_MSG_LOG_ERROR)
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_MISC_ERROR);
    va_end(args);
}*/

void __stdcall main(HINSTANCE h_mod)
{
    ch_send_ctx ctx = {
        .module_ = h_mod,
        .pipe = INVALID_HANDLE_VALUE,
    };
    ch_connect_client_pipe(&ctx);
    ch_comm_msg_type hello = CH_MSG_HELLO;
    ch_send_msg(&ctx, &hello, sizeof hello);

    /*ch_mod_info mod_infos[CH_MOD_COUNT];
    if (!ch_get_module_info(mod_infos))
        return;
    ch_find_datamap_by_name(&mod_infos[CH_MOD_ENGINE], "GAME_HEADER");*/

    ch_comm_msg_type bye = CH_MSG_GOODBYE;
    ch_send_msg(&ctx, &bye, sizeof bye);
    ch_clean_exit(&ctx, CH_PAYLOAD_EXIT_OK);
}

BOOL WINAPI DllMain(HINSTANCE handle, DWORD reason, LPVOID reserved)
{
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            if (!DisableThreadLibraryCalls(handle))
                return false;
            if (!CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)main, handle, 0, 0))
                return false;
            return true;
        case DLL_PROCESS_DETACH:
            (void)reserved;
            // TODO free associated memory (if reserved is null)
            return true;
        default:
            return true;
    }
}
