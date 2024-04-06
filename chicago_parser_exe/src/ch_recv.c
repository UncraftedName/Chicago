#include "ch_recv.h"
#include "SDK/datamap.h"

typedef struct ch_mp_unpacked_ll {
    msgpack_unpacked unp;
    struct ch_mp_unpacked_ll* next;
    char _pad[4];
} ch_mp_unpacked_ll;

typedef struct ch_process_msg_ctx {
    bool got_hello;
    char _pad[3];
    ch_log_level log_level;
    msgpack_unpacker mp_unpacker;
    ch_mp_unpacked_ll* unpacked_ll;
    size_t buf_expand_size;
    size_t msg_len;

    // TODO check perf, then try with arena :p
    // ch_arena arena;
} ch_process_msg_ctx;

ch_process_msg_ctx* ch_msg_ctx_alloc(ch_log_level log_level, size_t init_chunk_size)
{
    ch_process_msg_ctx* ctx = calloc(1, sizeof(ch_process_msg_ctx));
    if (!ctx)
        return NULL;
    ctx->log_level = log_level;
    ctx->unpacked_ll = calloc(1, sizeof(ch_mp_unpacked_ll));
    if (!ctx->unpacked_ll) {
        ch_msg_ctx_free(ctx);
        return NULL;
    }
    if (!msgpack_unpacker_init(&ctx->mp_unpacker, init_chunk_size)) {
        ch_msg_ctx_free(ctx);
        return NULL;
    }
    ctx->buf_expand_size = init_chunk_size;
    return ctx;
}

void ch_msg_ctx_free(ch_process_msg_ctx* ctx)
{
    if (!ctx)
        return;
    while (ctx->unpacked_ll) {
        ch_mp_unpacked_ll* cur = ctx->unpacked_ll;
        ctx->unpacked_ll = ctx->unpacked_ll->next;
        msgpack_unpacked_destroy(&cur->unp);
        free(cur);
    }
    msgpack_unpacker_destroy(&ctx->mp_unpacker);
    free(ctx);
}

char* ch_msg_ctx_buf(ch_process_msg_ctx* ctx)
{
    return msgpack_unpacker_buffer(&ctx->mp_unpacker);
}

size_t ch_msg_ctx_buf_capacity(ch_process_msg_ctx* ctx)
{
    return msgpack_unpacker_buffer_capacity(&ctx->mp_unpacker);
}

bool ch_msg_ctx_buf_expand(ch_process_msg_ctx* ctx)
{
    if (!msgpack_unpacker_reserve_buffer(&ctx->mp_unpacker, ctx->buf_expand_size))
        return false;
    ctx->buf_expand_size *= 2;
    return true;
}

void ch_msg_ctx_buf_consumed(ch_process_msg_ctx* ctx, size_t n)
{
    msgpack_unpacker_buffer_consumed(&ctx->mp_unpacker, n);
    ctx->msg_len += n;
}

typedef enum ch_unpack_result {
    // all is well
    CH_UNPACK_OK,
    // got goodbye, we can stop now
    CH_UNPACK_EXIT,
    // callee reports error
    CH_UNPACK_CONSTRAINT_FAIL,
    // caller reports error
    CH_UNPACK_OUT_OF_MEMORY,
    CH_UNPACK_WRONG_FORMAT,
} ch_unpack_result;

#define CH_CHECK(cond)                     \
    do {                                   \
        if (!(cond)) {                     \
            assert(0);                     \
            return CH_UNPACK_WRONG_FORMAT; \
        }                                  \
    } while (0)

#define CH_CHECK_STR(mp_str_obj, comp_str)                                                   \
    do {                                                                                     \
        CH_CHECK((mp_str_obj).type == MSGPACK_OBJECT_STR);                                   \
        CH_CHECK(!strncmp((mp_str_obj).via.str.ptr, comp_str, ((mp_str_obj).via.str).size)); \
    } while (0)

#define CH_CHECK_SCHEMA(expr)                 \
    do {                                      \
        ch_unpack_result schema_check = expr; \
        if (schema_check != CH_UNPACK_OK)     \
            return schema_check;              \
    } while (0)

typedef struct ch_kv_pair {
    const char* key_name;
    const uint8_t allowed_types[2];
    uint8_t n_allowed;
    uint8_t _pad;
} ch_kv_pair;

typedef struct ch_kv_schema {
    size_t n_kv_pairs;
    const ch_kv_pair* kv_pairs;
} ch_kv_schema;

#define CH_KV_SINGLE(name, msgpack_type)                                  \
    {                                                                     \
        .key_name = name, .allowed_types = {msgpack_type}, .n_allowed = 1 \
    }

#define CH_KV_EITHER(name, msgpack_type1, msgpack_type2)                                  \
    {                                                                                     \
        .key_name = name, .allowed_types = {msgpack_type1, msgpack_type2}, .n_allowed = 2 \
    }

#define CH_KV_WILD(name)                 \
    {                                    \
        .key_name = name, .n_allowed = 0 \
    }

#define CH_DEFINE_KV_SCHEMA(schema_name, ...)                      \
    static const ch_kv_pair schema_name##_pairs[] = {__VA_ARGS__}; \
    static const ch_kv_schema schema_name = {                      \
        .n_kv_pairs = ARRAYSIZE(schema_name##_pairs),              \
        .kv_pairs = schema_name##_pairs,                           \
    };

ch_unpack_result ch_check_kv_schema(msgpack_object o, ch_kv_schema schema)
{
    CH_CHECK(o.type == MSGPACK_OBJECT_MAP);
    CH_CHECK(o.via.map.size == schema.n_kv_pairs);
    const msgpack_object_kv* kv = o.via.map.ptr;
    for (size_t i = 0; i < schema.n_kv_pairs; i++) {
        CH_CHECK_STR(kv[i].key, schema.kv_pairs[i].key_name);
        for (size_t j = 0; j < schema.kv_pairs[i].n_allowed; j++)
            CH_CHECK(kv[i].val.type == schema.kv_pairs[i].allowed_types[j]);
    }
    return CH_UNPACK_OK;
}

/*
* case 1: root map is already present -> don't keep, verify full
* case 2: inherited are present -> keep, verify base map
* case 3: embedded maps are present -> keep, verify embedded
* case 3: no datamaps are present -> keep, no verify0
*/

enum ch_dm_process_type {
    // level 0
    CH_DM_COMPARE_WITH_SAVED,
    CH_DM_VERIFY_SCHEMA,
};

static ch_unpack_result ch_check_dm_schema(ch_process_msg_ctx* ctx, msgpack_object o, bool unpack)
{
    CH_DEFINE_KV_SCHEMA(dm_kv_schema,
                        CH_KV_SINGLE(CHMPK_MSG_DM_NAME, MSGPACK_OBJECT_STR),
                        CH_KV_SINGLE(CHMPK_MSG_DM_MODULE, MSGPACK_OBJECT_STR),
                        CH_KV_SINGLE(CHMPK_MSG_DM_MODULE_OFF, MSGPACK_OBJECT_POSITIVE_INTEGER),
                        CH_KV_EITHER(CHMPK_MSG_DM_BASE, MSGPACK_OBJECT_MAP, MSGPACK_OBJECT_NIL),
                        CH_KV_SINGLE(CHMPK_MSG_DM_FIELDS, MSGPACK_OBJECT_ARRAY));
    CH_CHECK_SCHEMA(ch_check_kv_schema(o, dm_kv_schema));

    const msgpack_object_kv* kv_dm = o.via.map.ptr;
    if (kv_dm[3].val.type == MSGPACK_OBJECT_MAP)
        CH_CHECK_SCHEMA(ch_check_dm_schema(ctx, kv_dm[3].val, unpack));

    for (size_t i = 0; i < kv_dm[4].val.via.array.size; i++) {
        msgpack_object td = kv_dm[4].val.via.array.ptr[i];
        CH_DEFINE_KV_SCHEMA(dm_td_schema,
                            CH_KV_SINGLE(CHMPK_MSG_TD_NAME, MSGPACK_OBJECT_STR),
                            CH_KV_SINGLE(CHMPK_MSG_TD_TYPE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_FLAGS, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_OFF, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_TOTAL_SIZE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_RESTORE_OPS, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_INPUT_FUNC, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_EITHER(CHMPK_MSG_TD_EMBEDDED, MSGPACK_OBJECT_MAP, MSGPACK_OBJECT_NIL),
                            CH_KV_SINGLE(CHMPK_MSG_TD_OVERRIDE_COUNT, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_TOL, MSGPACK_OBJECT_FLOAT));
        CH_CHECK_SCHEMA(ch_check_kv_schema(td, dm_td_schema));

        const msgpack_object_kv* td_kv = td.via.map.ptr;
        if (td_kv[7].val.type == MSGPACK_OBJECT_MAP)
            CH_CHECK_SCHEMA(ch_check_dm_schema(ctx, td_kv[7].val, unpack));
    }
    return CH_UNPACK_OK;
}

// static ch_unpack_result()

static ch_unpack_result ch_process_msg(ch_process_msg_ctx* ctx, msgpack_object o)
{
    CH_DEFINE_KV_SCHEMA(msg_kv_schema,
                        CH_KV_SINGLE(CHMPK_MSG_TYPE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                        CH_KV_WILD(CHMPK_MSG_DATA));

    ch_unpack_result schema_check = ch_check_kv_schema(o, msg_kv_schema);
    if (schema_check != CH_UNPACK_OK)
        return schema_check;

    const msgpack_object_kv* kv = o.via.map.ptr;

    ch_comm_msg_type msg_type = kv[0].val.via.u64;
    if (msg_type != CH_MSG_HELLO && !ctx->got_hello) {
        CH_LOG_ERROR(ctx, "Payload sent other messages before sending HELLO.");
        return CH_UNPACK_CONSTRAINT_FAIL;
    }
    switch (msg_type) {
        case CH_MSG_HELLO:
            CH_CHECK(kv[1].val.type == MSGPACK_OBJECT_NIL);
            CH_LOG_INFO(ctx, "Got HELLO message from payload.\n");
            ctx->got_hello = true;
            return CH_UNPACK_OK;
        case CH_MSG_GOODBYE:
            CH_CHECK(kv[1].val.type == MSGPACK_OBJECT_NIL);
            CH_LOG_INFO(ctx, "Got GOODBYE message from payload.\n");
            return CH_UNPACK_EXIT;
        case CH_MSG_LOG_INFO:
        case CH_MSG_LOG_ERROR:
            CH_CHECK(kv[1].val.type == MSGPACK_OBJECT_STR);
            ch_log_level level = msg_type == CH_MSG_LOG_INFO ? CH_LL_INFO : CH_LL_ERROR;
            CH_LOG_LEVEL(level, ctx, "[payload] %.*s\n", kv[1].val.via.str.size, kv[1].val.via.str.ptr);
            return CH_UNPACK_OK;
        case CH_MSG_DATAMAP:
        case CH_MSG_LINKED_NAME:
        default:
            return CH_UNPACK_WRONG_FORMAT;
    }
}

bool ch_msg_ctx_process(ch_process_msg_ctx* ctx)
{
    msgpack_unpack_return ret = msgpack_unpacker_next(&ctx->mp_unpacker, &ctx->unpacked_ll->unp);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        CH_LOG_ERROR(ctx, "msgpack_unpack failed with return %d.", ret);
        return false;
    }

#if 0
    CH_LOG_INFO(ctx, "[exe] recv msg (%zu bytes)\n[exe] ", ctx->msg_len);
    msgpack_object_print(stdout, ctx->unpacked_ll->unp.data);
    fprintf(stdout, "\n");
#endif

    switch (ch_process_msg(ctx, ctx->unpacked_ll->unp.data)) {
        case CH_UNPACK_OK:
            break;
        case CH_UNPACK_EXIT:
        case CH_UNPACK_CONSTRAINT_FAIL:
            return false;
        case CH_UNPACK_WRONG_FORMAT:
            CH_LOG_ERROR(ctx, "Received wrong msgpack format from payload.");
            return false;
        case CH_UNPACK_OUT_OF_MEMORY:
            CH_LOG_ERROR(ctx, "Out of memory while unpacking message from payload.");
            return false;
        default:
            assert(0);
            return false;
    }

    // TODO: only free the unpacked data if necessary
    msgpack_unpacked_destroy(&ctx->unpacked_ll->unp);
    ctx->msg_len = 0;

    return true;
}
