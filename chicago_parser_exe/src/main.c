#include <stdio.h>
#include <string.h>

#pragma comment(lib, "Pathcch.lib")

#include "ch_save.h"
#include "ch_inject.h"

#define MAX_SELECT_GAMES 9

int main(void)
{
    ch_parsed_save_data save_data;
    ch_err err = ch_parse_save_path(&save_data, "G:/Games/portal/Portal Source/portal/SAVE/00.sav");
    if (err)
        printf("Parsing failed with error: %s\n", ch_err_strs[err]);
    else
        printf("Test result: %.4s\n", save_data.tag.id);
    ch_free_parsed_save_data(&save_data);

    // ch_search_result result;
    // ch_search_pe_file(&result);

    PROCESSENTRY32W candidate_games[MAX_SELECT_GAMES];
    HANDLE game_handles[MAX_SELECT_GAMES];
    int num_found_games;
    ch_find_candidate_games(candidate_games, MAX_SELECT_GAMES, &num_found_games);

    if (num_found_games == 0) {
        printf("No candidate games found!\n");
    } else {
        DWORD handle_flags = PROCESS_CREATE_THREAD | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION |
                             PROCESS_VM_OPERATION;
        for (int i = 0; i < num_found_games; i++) {
            game_handles[i] = OpenProcess(handle_flags, false, candidate_games[i].th32ProcessID);
            if (!game_handles[i])
                continue;
            wchar_t exe_path[MAX_PATH];
            DWORD len = GetModuleFileNameExW(game_handles[i], NULL, exe_path, MAX_PATH);
            if (!len)
                continue;
            wprintf(L"Candidate game: (PID: %ld), %s\n", candidate_games[i].th32ProcessID, exe_path);
        }
        // just chose the first one for now: TODO build a pretty menu here
        // TODO unload the payload if it's already in the game here
        // TODO check if the dll exists on disk (ideally we should check that before finding a game, or maybe we could just try loading and not worry about it)
        // TODO free this memory using VirtualFreeEx
        wchar_t payload_path[MAX_PATH];
        LPVOID alloc = VirtualAllocEx(game_handles[0], NULL, sizeof payload_path, MEM_COMMIT, PAGE_READWRITE);
        if (!alloc)
            return;
        GetModuleFileNameW(NULL, payload_path, MAX_PATH);
        PathCchRemoveFileSpec(payload_path, MAX_PATH);
        // TODO this is hardcoded for now, any way to move it to a #define?
        PathCchCombine(payload_path, MAX_PATH, payload_path, L"chicago_payload.dll");
        BOOL ret = WriteProcessMemory(game_handles[0], alloc, payload_path, sizeof payload_path, NULL);
        if (!ret)
            return;
        DWORD threadId;
        HANDLE thread =
            CreateRemoteThread(game_handles[0], NULL, 0, (LPTHREAD_START_ROUTINE)(void*)LoadLibraryW, alloc, 0, &threadId);
        if (!thread)
            return;
    }

    return 0;
}
