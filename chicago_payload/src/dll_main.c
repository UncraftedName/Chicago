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

    // PIMAGE_NT_HEADERS headers = ImageNtHeader(ctx->mod_infos[CH_MOD_SERVER].base);
    /*PIMAGE_SECTION_HEADER header;
    ULONG size;
    char* base = ctx->mod_infos[CH_MOD_ENGINE].base;
    PIMAGE_IMPORT_DESCRIPTOR import_dir =
        (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToDataEx(base, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &size, &header);

    for (; import_dir->FirstThunk; import_dir++) {

        PIMAGE_THUNK_DATA32 thunk = base + import_dir->OriginalFirstThunk;
        for (; thunk->u1.Function; thunk++) {
            if (!IMAGE_SNAP_BY_ORDINAL32(thunk->u1.AddressOfData)) {
                PIMAGE_IMPORT_BY_NAME import_name = base + thunk->u1.AddressOfData;
                ch_send_log_info(ctx,
                                 "DLL: %s, Symbol: %s, Hint: %d",
                                 base + import_dir->Name,
                                 import_name->Name,
                                 import_name->Hint);
            } else {
                ch_send_log_info(ctx,
                                 "DLL: %s, Ordinal: %d",
                                 base + import_dir->Name,
                                 IMAGE_ORDINAL32(thunk->u1.AddressOfData));
            }
        }
    }*/

    /*for (; import_dir->Characteristics; import_dir++) {

        PIMAGE_THUNK_DATA32 thunk = base + import_dir->OriginalFirstThunk;
        for (; thunk->u1.AddressOfData; thunk++) {
            PIMAGE_IMPORT_BY_NAME import_name = base + thunk->u1.AddressOfData;
            ch_send_log_info(ctx, "DLL: %s, Symbol: %s", base + import_dir->Name, import_name->Name);
        }
    }*/

    struct {
        const char* str;
        const void* p;
    } str_infos[] = {
        {.str = "dumpentityfactories"},
        {.str = "Lists all entity factory names."},
    };

    ch_mod_sec server_rdata = sc.mods[CH_MOD_SERVER].sections[CH_SEC_RDATA];
    for (int i = 0; i < 2; i++) {
        str_infos[i].p =
            ch_memmem_unique(server_rdata.start, server_rdata.len, str_infos[i].str, strlen(str_infos[i].str) + 1);
        if (!str_infos[i].p)
            ch_send_err_and_exit(ctx,
                                 "Failed to find string '%s' in the .rdata section in server.dll.",
                                 str_infos[i].str);
        if (str_infos[i].p == (void*)(-1))
            ch_send_err_and_exit(ctx,
                                 "Found multiple of string '%s' in the .rdata section in server.dll (expected 1).",
                                 str_infos[i].str);
    }

    ch_mod_sec server_text = sc.mods[CH_MOD_SERVER].sections[CH_SEC_TEXT];
    const char* middle_of_dumpentityfactories_ctor = NULL;
    const char* search_start = server_text.start;
    size_t search_len = server_text.len;
    const char* search_end = search_start + server_text.len;
    while (search_start < search_end) {
        const char* str_usage1 = ch_memmem(search_start, search_len, &str_infos[0].p, sizeof str_infos[0].p);
        if (!str_usage1)
            break;
        const char* search_start2 = max((char*)server_text.start, str_usage1 - 32);
        const char* search_end2 = min(search_end, str_usage1 + 32);
        const char* str_usage2 =
            ch_memmem(search_start2, (size_t)(search_end2 - search_start2), &str_infos[1].p, sizeof str_infos[1].p);

        if (str_usage1 && str_usage2) {
            if (middle_of_dumpentityfactories_ctor)
                ch_send_err_and_exit(
                    ctx,
                    "Found multiple candidates for 'dumpentityfactories' ConCommand ctor (expected 1).");
            middle_of_dumpentityfactories_ctor = str_usage1;
        }

        size_t diff = (size_t)(str_usage1 - search_start) + 1;
        search_start += diff;
        search_len -= diff;
    }
    if (!middle_of_dumpentityfactories_ctor)
        ch_send_err_and_exit(ctx, "Found no candidates for 'dumpentityfactories' ConCommand ctor.");


    ch_send_log_info(ctx, "FOUND SOMETHING at %08X!!!", middle_of_dumpentityfactories_ctor - (char*)sc.mods[CH_MOD_SERVER].base);

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
