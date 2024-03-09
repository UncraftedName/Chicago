#include "ch_payload_comm.h"

#include <DbgHelp.h>

#pragma comment(lib, "dbghelp.lib")

bool ch_get_module_info(DWORD proc_id, ch_mod_info info[CH_MOD_COUNT])
{
    HANDLE snap = (HANDLE)ERROR_BAD_LENGTH;
    for (int i = 0; i < 10 && snap == (HANDLE)ERROR_BAD_LENGTH; i++)
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, proc_id);
    if (snap == (HANDLE)ERROR_BAD_LENGTH || snap == INVALID_HANDLE_VALUE)
        return false;

    unsigned int mod_flags = 0, target_flags = (1 << CH_MOD_COUNT) - 1;

    MODULEENTRY32W me32w = {.dwSize = sizeof(MODULEENTRY32W)};
    if (Module32FirstW(snap, &me32w)) {
        do {
            for (int m = 0; m < CH_MOD_COUNT; m++) {
                if (wcscmp(ch_required_module_names[m], me32w.szModule))
                    continue;
                mod_flags |= 1 << m;
                if (info) {
                    PIMAGE_NT_HEADERS header = ImageNtHeader(me32w.modBaseAddr);
                    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(header);
                    for (int fs = 0; fs < header->FileHeader.NumberOfSections; fs++) {
                        for (int ms = 0; ms < CH_SEC_COUNT; ms++) {
                            if (!_stricmp((const char*)section->Name, ch_mod_sec_names[ms])) {
                                info[m].sections[ms].start = me32w.modBaseAddr + section->VirtualAddress;
                                info[m].sections[ms].len = section->Misc.VirtualSize;
                            }
                        }
                        ++section;
                    }
                }
                break;
            }
        } while (mod_flags != target_flags && Module32NextW(snap, &me32w));
    }
    CloseHandle(snap);
    return mod_flags == target_flags;
}
