#pragma once

#include "ch_payload_comm.h"

// finds as many processes as possible which may be a valid source game
void ch_find_candidate_games(PROCESSENTRY32W* procs, int num_entries, int* num_entries_returned);
