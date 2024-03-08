

#include "ch_inject.h"
#include "ch_search.h"

void ch_find_candidate_games(PROCESSENTRY32W* procs, int num_entries, int* num_entries_returned)
{
    *num_entries_returned = 0;
    if (!procs || num_entries <= 0)
        return;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;

    procs[0].dwSize = sizeof *procs;
    if (Process32FirstW(snap, &procs[0])) {
        do {
            if (ch_proc_has_required_modules(procs[*num_entries_returned].th32ProcessID)) {
                if (++*num_entries_returned >= num_entries)
                    break;
                procs[*num_entries_returned].dwSize = sizeof *procs;
            }
        } while (Process32NextW(snap, &procs[*num_entries_returned]));
    }
    CloseHandle(snap);
}
