#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "ch_payload_comm_shared.h"

// dbg_level should be either DEBUG or ERROR
void ch_do_inject_and_recv_maps(ch_comm_msg_type dbg_level);

// finds as many processes as possible which may be a valid source game
// TODO set num_entries returned to the total number found, even if it's bigger
void ch_find_candidate_games(DWORD* proc_ids, int num_entries, int* num_entries_returned);
