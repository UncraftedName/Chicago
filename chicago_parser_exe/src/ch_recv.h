#pragma once

#include "ch_payload_comm_shared.h"
#include "ch_args.h"

// a naked
typedef enum ch_datamap_collection_type {
    // appends to collections.zip.br (or creates if it doesn't exist) for internal use in the parser (compressed)
    CH_DC_ARCHIVE_NAKED,
    // creates a collection.ch file for internal use in the parser (not compressed)
    CH_DC_STRUCT_NAKED,
    // creates a readable .msgpack file for external use (not compressed)
    CH_DC_STRUCT_MSGPACK,
} ch_datamap_collection_type;

typedef struct ch_datamap_collection_info {
    const char* output_file_path;
    const char* game_name;
    const char* game_version;
    ch_datamap_collection_type output_type;
} ch_datamap_collection_info;

struct ch_process_msg_ctx;

struct ch_process_msg_ctx* ch_msg_ctx_alloc(ch_log_level log_level,
                                            size_t init_chunk_size,
                                            const ch_datamap_collection_info* collection_save_info);
void ch_msg_ctx_free(struct ch_process_msg_ctx* ctx);

// The receive logic will call these functions to fill the internal buffer.
// In practice these are just wrappers of the equivalent msgpack functions.
char* ch_msg_ctx_buf(struct ch_process_msg_ctx* ctx);
size_t ch_msg_ctx_buf_capacity(struct ch_process_msg_ctx* ctx);
bool ch_msg_ctx_buf_expand(struct ch_process_msg_ctx* ctx);
void ch_msg_ctx_buf_consumed(struct ch_process_msg_ctx* ctx, size_t n);

// process data in the internal buffers, return true if we're expecting more messages
bool ch_msg_ctx_process(struct ch_process_msg_ctx* ctx);

// do everything - inject the dll into a source game, receive datamaps, and write to file
void ch_do_inject_and_recv_maps(const ch_datamap_collection_info* collection_save_info, ch_log_level log_level);
