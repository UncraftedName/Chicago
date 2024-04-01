#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <libloaderapi.h>

#include <stdio.h>

#include "ch_search.h"
#include "ch_msgpack.h"

typedef struct ch_send_ctx {
    HANDLE module_;
    HANDLE pipe;
    msgpack_sbuffer mp_buf;
    msgpack_packer mp_pk;

    // CreateInterfaceFn vstdlib_factory;
    // ICvar* g_pCVar;
} ch_send_ctx;

// checked msgpack pack
#define CH_CHK_PACK(x)                  \
    {                                   \
        int _mp_ret = msgpack_pack_##x; \
        if (_mp_ret != 0)               \
            return _mp_ret;             \
    }

void ch_send_wave(ch_send_ctx* ctx, ch_comm_msg_type type);
void ch_send_log_info(ch_send_ctx* ctx, const char* fmt, ...);
// slightly different params than other functions to match with datamap callback
void ch_send_datamap(const datamap_t* dm, void* ctx);
__declspec(noreturn) void ch_send_err_and_exit(ch_send_ctx* ctx, const char* fmt, ...);
__declspec(noreturn) void ch_clean_exit(ch_send_ctx* ctx, DWORD exit_code);
