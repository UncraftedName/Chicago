#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <libloaderapi.h>

#include <stdio.h>

#include "ch_search.h"
#include "ch_msgpack.h"

// checked msgpack pack
#define CH_CHK_PACK(x)                  \
    {                                   \
        int _mp_ret = msgpack_pack_##x; \
        if (_mp_ret != 0)               \
            return _mp_ret;             \
    }

typedef struct ch_send_ctx {
    HANDLE module_;
    HANDLE pipe;
    msgpack_sbuffer mp_buf;
    msgpack_packer mp_pk;
} ch_send_ctx;

static void ch_clean_exit(ch_send_ctx* ctx, ch_payload_exit_code exit_code)
{
    if (ctx->pipe != INVALID_HANDLE_VALUE)
        CloseHandle(ctx->pipe);
    msgpack_sbuffer_destroy(&ctx->mp_buf);
    FreeLibraryAndExitThread(ctx->module_, exit_code);
}

static void ch_connect_pipe(ch_send_ctx* ctx)
{
    if (!WaitNamedPipeA(CH_PIPE_NAME, CH_PIPE_TIMEOUT_MS)) {
        CH_PL_PRINTF("WaitNamedPipe failed (GLE=%lu)\n", GetLastError());
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_PIPE_WAIT_FAIL);
    }
    ctx->pipe = CreateFileA(CH_PIPE_NAME, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (ctx->pipe == INVALID_HANDLE_VALUE) {
        CH_PL_PRINTF("CreateFileA failed (GLE=%lu)\n", GetLastError());
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_PIPE_CONNECT_FAIL);
    }
}

static void ch_send_msgpack(ch_send_ctx* ctx)
{
    BOOL success = WriteFile(ctx->pipe, ctx->mp_buf.data, ctx->mp_buf.size, NULL, NULL);
    if (!success)
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_PIPE_SEND_FAIL);
}

// inits the msg with {"type": type, "data": ...}
static int ch_msg_preamble(ch_send_ctx* ctx, ch_comm_msg_type type)
{
    msgpack_sbuffer_clear(&ctx->mp_buf);
    msgpack_packer* pk = &ctx->mp_pk;
    CH_CHK_PACK(map(pk, 2));
    CH_CHK_PACK(str_with_body(pk, CHMPK_MSG_TYPE, strlen(CHMPK_MSG_TYPE)));
    CH_CHK_PACK(int(pk, type));
    CH_CHK_PACK(str_with_body(pk, CHMPK_MSG_DATA, strlen(CHMPK_MSG_DATA)));
    return 0;
}

static void ch_send_simple(ch_send_ctx* ctx, ch_comm_msg_type type)
{
    assert(type == CH_MSG_HELLO || type == CH_MSG_GOODBYE);
    if (ch_msg_preamble(ctx, type) || msgpack_pack_nil(&ctx->mp_pk))
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_OUT_OF_MEMORY);
    ch_send_msgpack(ctx);
}

static void ch_send_vstrf(ch_send_ctx* ctx, ch_comm_msg_type level, const char* fmt, va_list vargs)
{
    assert(level == CH_MSG_LOG_INFO || level == CH_MSG_LOG_ERROR);
    char buf[1024];
    int len = vsnprintf(buf, sizeof buf, fmt, vargs);
    len = min(sizeof(buf) - 1, len);
    if (ch_msg_preamble(ctx, level) || msgpack_pack_str_with_body(&ctx->mp_pk, buf, len))
        ch_clean_exit(ctx, CH_PAYLOAD_EXIT_OUT_OF_MEMORY);
    ch_send_msgpack(ctx);
}

static void ch_send_log_info(ch_send_ctx* ctx, const char* fmt, ...)
{
    va_list vargs;
    va_start(vargs, fmt);
    ch_send_vstrf(ctx, CH_MSG_LOG_INFO, fmt, vargs);
    va_end(vargs);
}

static void ch_send_err_and_exit(ch_send_ctx* ctx, ch_payload_exit_code exit_code, const char* fmt, ...)
{
    va_list vargs;
    va_start(vargs, fmt);
    ch_send_vstrf(ctx, CH_MSG_LOG_ERROR, fmt, vargs);
    va_end(vargs);
    ch_clean_exit(ctx, exit_code);
}

void __stdcall main(HINSTANCE h_mod)
{
    CH_PL_PRINTF("main\n");
    ch_send_ctx rctx = {
        .module_ = h_mod,
        .pipe = INVALID_HANDLE_VALUE,
    };

    msgpack_sbuffer_init(&rctx.mp_buf);
    msgpack_packer_init(&rctx.mp_pk, &rctx.mp_buf, msgpack_sbuffer_write);

    ch_send_ctx* ctx = &rctx;

    ch_connect_pipe(ctx);
    ch_send_simple(ctx, CH_MSG_HELLO);

    /*ch_mod_info mod_infos[CH_MOD_COUNT];
    if (!ch_get_module_info(mod_infos)) {
        // send error here :)
    }
    ch_find_datamap_by_name(&mod_infos[CH_MOD_ENGINE], "GAME_HEADER");*/

    ch_send_simple(ctx, CH_MSG_GOODBYE);
    ch_clean_exit(ctx, CH_PAYLOAD_EXIT_OK);
}

BOOL WINAPI DllMain(HINSTANCE handle, DWORD reason, LPVOID reserved)
{
    CH_PL_PRINTF("DllMain (reason: %lu)\n", reason);
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            if (!DisableThreadLibraryCalls(handle)) {
                CH_PL_PRINTF("DisableThreadLibraryCalls failed (GLE: %lu), exiting\n", GetLastError());
                return false;
            }
            if (!CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)main, handle, 0, 0)) {
                CH_PL_PRINTF("CreateThreadfailed (GLE: %lu), exiting\n", GetLastError());
                return false;
            }
            return true;
        case DLL_PROCESS_DETACH:
            (void)reserved;
            // TODO free associated memory (if reserved is null)
            return true;
        default:
            return true;
    }
}
