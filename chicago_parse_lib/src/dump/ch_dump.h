#pragma once

#include <stdio.h>

#include "thirdparty/msgpack/include/ch_msgpack.h"
#include "ch_save_internal.h"

typedef struct ch_dump_text {
    FILE* f;
    uint8_t indent_lvl;
    bool pending_nl;
    uint8_t indent_str_len;
    char _pad[1];
    const char* indent_str;
    ch_dump_flags flags;
} ch_dump_text;

ch_err ch_dump_text_printf(ch_dump_text* dump, const char* fmt, ...);
ch_err ch_dump_text_flush_nl(ch_dump_text* dump);

typedef struct ch_dump_msgpack {
    msgpack_packer pk;
    ch_dump_flags flags;
} ch_dump_msgpack;

#define CH_DUMP_MP_CHECKED(dump, mp_op) \
    do {                                \
        int _ret = mp_op;               \
        if (_ret != 0)                  \
            return CH_ERR_MSGPACK;      \
    } while (0)

#define CH_DUMP_MP_STR_CHECKED(dump, str) \
    CH_DUMP_MP_CHECKED(dump, msgpack_pack_str_with_body(&(dump)->pk, str, strlen(str)))

typedef ch_err (*ch_dump_text_fn)(ch_dump_text* dump, void* user_data);
typedef ch_err (*ch_dump_msgpack_fn)(ch_dump_msgpack* dump, void* user_data);

typedef struct ch_dump_fns {
    ch_dump_text_fn text;
    ch_dump_msgpack_fn msgpack;
} ch_dump_fns;

extern const ch_dump_fns g_dump_default_fns;

#define _CH_DUMP_CALL(dump_fns, dump, user_data, dump_type) \
    (dump_fns.dump_type ? dump_fns.dump_type(dump, user_data) : g_dump_default_fns.dump_type(dump, #dump_fns))

#define CH_DUMP_TEXT_CALL(dump_fns, dump, user_data) _CH_DUMP_CALL(dump_fns, dump, user_data, text)
#define CH_DUMP_MSGPACK_CALL(dump_fns, dump, user_data) _CH_DUMP_CALL(dump_fns, dump, user_data, msgpack)
