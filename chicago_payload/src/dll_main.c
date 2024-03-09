#include "ch_search.h"

void __stdcall main(HINSTANCE handle)
{
    ch_mod_info mod_infos[CH_MOD_COUNT];
    if (!ch_get_module_info(0, mod_infos))
        return; // TOOD error here somehow
    ch_find_datamap_by_name(&mod_infos[CH_MOD_ENGINE], "GAME_HEADER");
    FreeLibraryAndExitThread(handle, 0);
}

BOOL WINAPI DllMain(HINSTANCE handle, DWORD reason, LPVOID reserved)
{
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            if (!DisableThreadLibraryCalls(handle))
                return false;
            // if (!ch_proc_has_required_modules(GetCurrentProcessId()))
            //   return false;
            HANDLE main_handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)main, handle, 0, 0);
            if (!main_handle)
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
