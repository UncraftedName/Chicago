#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <libloaderapi.h>

#include <stdio.h>

#include "ch_search.h"

#include "thirdparty/msgpack/include/ch_msgpack.h"

typedef struct ch_send_ctx {
    HANDLE module_;
    HANDLE pipe;
    msgpack_sbuffer mp_buf;
    msgpack_packer mp_pk;
} ch_send_ctx;


void ch_send_msgpack(ch_send_ctx* ctx);
void ch_send_wave(ch_send_ctx* ctx, ch_comm_msg_type type);
void ch_send_log_info(ch_send_ctx* ctx, const char* fmt, ...);

#define CH_PAYLOAD_LOG_INFO(ctx, fmt, ...) ch_send_log_info(ctx, "[%s]: " fmt ".", __FUNCTION__, __VA_ARGS__)
#define CH_PAYLOAD_LOG_ERR(ctx, fmt, ...) ch_send_err_and_exit(ctx, "[%s]: " fmt ".", __FUNCTION__, __VA_ARGS__)

int ch_msg_preamble(ch_send_ctx* ctx, ch_comm_msg_type type);

typedef struct ch_send_datamap_cb_udata {
    ch_send_ctx* send_ctx;
    ch_search_ctx* sc;
    ch_game_module mod_idx;
} ch_send_datamap_cb_udata;

void ch_send_datamap_cb(const datamap_t* dm, void* info);
__declspec(noreturn) void ch_send_err_and_exit(ch_send_ctx* ctx, const char* fmt, ...);
__declspec(noreturn) void ch_clean_exit(ch_send_ctx* ctx, DWORD exit_code);
