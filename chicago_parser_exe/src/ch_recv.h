#pragma once

#include "ch_payload_comm_shared.h"
#include "ch_args.h"
#include "ch_msgpack.h"

struct ch_process_msg_ctx;

struct ch_process_msg_ctx* ch_msg_ctx_alloc(ch_log_level log_level,
                                            size_t init_chunk_size,
                                            const char* output_file_path);
void ch_msg_ctx_free(struct ch_process_msg_ctx* ctx);
char* ch_msg_ctx_buf(struct ch_process_msg_ctx* ctx);
size_t ch_msg_ctx_buf_capacity(struct ch_process_msg_ctx* ctx);
bool ch_msg_ctx_buf_expand(struct ch_process_msg_ctx* ctx);
void ch_msg_ctx_buf_consumed(struct ch_process_msg_ctx* ctx, size_t n);

// return true if we are expecting more messages
bool ch_msg_ctx_process(struct ch_process_msg_ctx* ctx);

void ch_do_inject_and_recv_maps(const char* file_output_path, ch_log_level log_level);
