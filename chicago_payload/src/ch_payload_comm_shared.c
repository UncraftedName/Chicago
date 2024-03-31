#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>

#include "ch_payload_comm_shared.h"
#include "ch_search.h"

bool ch_get_required_modules(DWORD proc_id, BYTE* base_addresses[CH_MOD_COUNT])
{
    HANDLE snap = (HANDLE)ERROR_BAD_LENGTH;
    for (int i = 0; i < 10 && snap == (HANDLE)ERROR_BAD_LENGTH; i++)
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, proc_id);
    if (snap == (HANDLE)ERROR_BAD_LENGTH || snap == INVALID_HANDLE_VALUE)
        return false;

    unsigned int mod_flags = 0, target_flags = (1 << CH_MOD_COUNT) - 1;

    MODULEENTRY32 me32 = {.dwSize = sizeof(MODULEENTRY32)};
    if (Module32First(snap, &me32)) {
        do {
            for (int i = 0; i < CH_MOD_COUNT; i++) {
                if (strcmp(ch_required_module_names[i], me32.szModule))
                    continue;
                mod_flags |= 1 << i;
                if (base_addresses)
                    base_addresses[i] = me32.modBaseAddr;
                break;
            }
        } while (mod_flags != target_flags && Module32Next(snap, &me32));
    }
    CloseHandle(snap);
    return mod_flags == target_flags;
}
