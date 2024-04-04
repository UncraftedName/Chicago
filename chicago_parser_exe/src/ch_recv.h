#pragma once

#include "ch_payload_comm_shared.h"
#include "ch_args.h"
#include "ch_msgpack.h"

typedef struct ch_process_msg_ctx {
    bool got_hello;
    char _pad[3];
    ch_log_level log_level;
    msgpack_zone mp_zone;
} ch_process_msg_ctx;

static inline bool ch_process_msg_ctx_init(ch_process_msg_ctx* ctx, ch_log_level log_level, size_t init_zone_chunk_size)
{
    ctx->log_level = log_level;
    memset(ctx, 0, sizeof *ctx);
    return msgpack_zone_init(&ctx->mp_zone, init_zone_chunk_size);
}

static void ch_process_msg_ctx_destroy(ch_process_msg_ctx* ctx)
{
    msgpack_zone_destroy(&ctx->mp_zone);
}

void ch_do_inject_and_recv_maps(ch_log_level log_level);

// return true if we are expecting more messages
bool ch_unpack_and_process_msg(ch_process_msg_ctx* ctx, const char* buf, size_t buf_size);
