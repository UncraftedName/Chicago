#include "ch_send.h"

#pragma warning(push, 3)
#include <DbgHelp.h>
#pragma warning(pop)

#pragma comment(lib, "dbghelp.lib")

void ch_clean_exit(ch_send_ctx* ctx, DWORD exit_code)
{
    // assert(exit_code == 0);
    if (ctx->pipe != INVALID_HANDLE_VALUE)
        CloseHandle(ctx->pipe);
    msgpack_sbuffer_destroy(&ctx->mp_buf);
    FreeLibraryAndExitThread(ctx->module_, exit_code);
}

static void ch_connect_pipe(ch_send_ctx* ctx)
{
    if (!WaitNamedPipeA(CH_PIPE_NAME, CH_PIPE_TIMEOUT_MS)) {
        CH_PL_PRINTF("WaitNamedPipe failed (GLE=%lu)\n", GetLastError());
        ch_clean_exit(ctx, 1);
    }
    ctx->pipe = CreateFileA(CH_PIPE_NAME, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (ctx->pipe == INVALID_HANDLE_VALUE) {
        CH_PL_PRINTF("CreateFileA failed (GLE=%lu)\n", GetLastError());
        ch_clean_exit(ctx, 1);
    }
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
    ch_send_wave(ctx, CH_MSG_HELLO);

    ch_search_ctx sc;

    ch_get_module_info(ctx, &sc);

    ch_find_entity_factory_cvar(ctx, &sc);
    ch_find_static_inits_from_single(ctx, &sc, CH_MOD_SERVER, sc.dump_entity_factories_ctor);
    ch_iterate_datamaps(ctx, &sc, CH_MOD_SERVER, ch_send_datamap, ctx);

    ch_send_wave(ctx, CH_MSG_GOODBYE);
    ch_clean_exit(ctx, 0);
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
