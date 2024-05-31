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
    ch_str_ll *first_error, *last_error;
    ch_arena* arena; // just for the errors
} ch_dump_text;

ch_err ch_dump_text_printf(ch_dump_text* dump, const char* fmt, ...);
ch_err ch_dump_text_flush_nl(ch_dump_text* dump);
ch_err ch_dump_text_log_err(ch_dump_text* dump, const char* fmt, ...);
#define CH_DUMP_TEXT_LOG_ERR(dump, fmt, ...) \
    CH_RET_IF_ERR(ch_dump_text_log_err(dump, "[%s]: " fmt ".", __FUNCTION__, __VA_ARGS__))

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

// if adding a new dump type, add it here
#define _CH_FOREACH_DUMP_TYPE(GEN, component_name, ...)  \
    GEN(text, ch_dump_text, component_name, __VA_ARGS__) \
    GEN(msgpack, ch_dump_msgpack, component_name, __VA_ARGS__)

#define _CH_TYPEDEF_DUMP_FN(dump_type, dump_struct, component_name, ...) \
    typedef ch_err (*_ch_dump_##component_name##_##dump_type##_fn)(dump_struct * dump, __VA_ARGS__);

#define _CH_LIST_DUMP_STRUCT_ELEM(dump_type, dump_struct, component_name, ...) \
    _ch_dump_##component_name##_##dump_type##_fn dump_type;

#define CH_DECLARE_DUMP_FNS(component_name, ...)                            \
    _CH_FOREACH_DUMP_TYPE(_CH_TYPEDEF_DUMP_FN, component_name, __VA_ARGS__) \
    typedef struct ch_dump_##component_name##_fns {                         \
        _CH_FOREACH_DUMP_TYPE(_CH_LIST_DUMP_STRUCT_ELEM, component_name)    \
    } ch_dump_##component_name##_fns;

#define CH_DECLARE_DUMP_FNS_SINGLE(component_name, global_fns_name, ...) \
    CH_DECLARE_DUMP_FNS(component_name, __VA_ARGS__)                     \
    extern const ch_dump_##component_name##_fns global_fns_name;

CH_DECLARE_DUMP_FNS_SINGLE(default, g_dump_default_fns, const char* dump_fns_name);

#define _CH_DUMP_CALL(dump_fns, dump, dump_type, ...) \
    ((dump_fns).dump_type ? (dump_fns).dump_type(dump, __VA_ARGS__) : g_dump_default_fns.dump_type(dump, #dump_fns))

// probably a good idea to define this if adding a new dump type
#define CH_DUMP_TEXT_CALL(dump_fns, dump, ...) _CH_DUMP_CALL(dump_fns, dump, text, __VA_ARGS__)
#define CH_DUMP_MSGPACK_CALL(dump_fns, dump, ...) _CH_DUMP_CALL(dump_fns, dump, msgpack, __VA_ARGS__)
