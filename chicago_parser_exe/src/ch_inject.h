#pragma once

// TODO all of these includes should be moved to a single file :)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <PathCch.h>

// finds as many processes as possible which may be a valid source game
void ch_find_candidate_games(PROCESSENTRY32W* procs, int num_entries, int* num_entries_returned);
