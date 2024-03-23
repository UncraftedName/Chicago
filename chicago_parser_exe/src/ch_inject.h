#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "ch_payload_comm_shared.h"

// dbg_level should be either DEBUG or ERROR
void ch_do_inject_and_recv_maps(ch_comm_msg_type dbg_level);
