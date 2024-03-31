#pragma once

#include "ch_payload_comm_shared.h"
#include "ch_args.h"
#include "ch_msgpack.h"

typedef struct ch_mp_recv_ctx {
    msgpack_unpacker up;
} ch_mp_recv_ctx;

// msgpack_unpacker_init

void ch_do_inject_and_recv_maps(ch_log_level log_level);
// void ch_process_msg();
