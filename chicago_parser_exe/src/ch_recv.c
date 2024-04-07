#include <stdio.h>
#include <errno.h>

#include "ch_recv.h"
#include "SDK/datamap.h"
#include "thirdparty/hashmap.h"

#include "msgpack/fbuffer.h"

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
    // dm name -> msgpack_object
    struct hashmap* dm_hashmap;
    const char* output_file_path;

    // TODO check perf, then try with arena :p
    // ch_arena arena;
} ch_process_msg_ctx;

typedef struct ch_hashmap_entry {
    msgpack_object_str name;
    msgpack_object o;
} ch_hashmap_entry;

static uint64_t ch_hashmap_entry_hash(const void* key, uint64_t seed0, uint64_t seed1)
{
    const ch_hashmap_entry* entry = key;
    return hashmap_xxhash3(entry->name.ptr, entry->name.size, seed0, seed1);
}

static inline int ch_cmp_mp_str(msgpack_object_str s1, msgpack_object_str s2)
{
    // I'd like strnicmp, but in theory that can break mega-hard if two maps have the same lowercase name
    int res = strncmp(s1.ptr, s2.ptr, min(s1.size, s2.size));
    if (res)
        return res;
    if (s1.size < s2.size)
        return -1;
    if (s1.size > s2.size)
        return 1;
    return 0;
}

static int ch_hashmap_entry_compare(const void* a, const void* b, void* udata)
{
    (void)udata;
    return ch_cmp_mp_str(((const ch_hashmap_entry*)a)->name, ((const ch_hashmap_entry*)b)->name);
}

ch_process_msg_ctx* ch_msg_ctx_alloc(ch_log_level log_level, size_t init_chunk_size, const char* output_file_path)
{
    ch_process_msg_ctx* ctx = calloc(1, sizeof(ch_process_msg_ctx));
    if (!ctx)
        return NULL;
    ctx->log_level = log_level;
    ctx->output_file_path = output_file_path;
    ctx->unpacked_ll = calloc(1, sizeof(ch_mp_unpacked_ll));
    if (!ctx->unpacked_ll)
        goto failed;
    if (!msgpack_unpacker_init(&ctx->mp_unpacker, init_chunk_size))
        goto failed;
    ctx->dm_hashmap =
        hashmap_new(sizeof(ch_hashmap_entry), 256, 0, 0, ch_hashmap_entry_hash, ch_hashmap_entry_compare, NULL, NULL);
    if (!ctx->dm_hashmap)
        goto failed;
    ctx->buf_expand_size = init_chunk_size;
    return ctx;
failed:
    ch_msg_ctx_free(ctx);
    return NULL;
}

void ch_msg_ctx_free(ch_process_msg_ctx* ctx)
{
    if (!ctx)
        return;
    if (ctx->dm_hashmap)
        hashmap_free(ctx->dm_hashmap);
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

#define CH_CHECK_FORMAT(cond)              \
    do {                                   \
        if (!(cond)) {                     \
            assert(0);                     \
            return CH_UNPACK_WRONG_FORMAT; \
        }                                  \
    } while (0)

#define CH_CHECK_STR_FORMAT(mp_str_obj, comp_str)                                                   \
    do {                                                                                            \
        CH_CHECK_FORMAT((mp_str_obj).type == MSGPACK_OBJECT_STR);                                   \
        CH_CHECK_FORMAT(!strncmp((mp_str_obj).via.str.ptr, comp_str, ((mp_str_obj).via.str).size)); \
    } while (0)

typedef struct ch_kv_pair {
    const char* key_name;
    const uint8_t allowed_types[2];
    uint8_t n_allowed;
    uint8_t _pad;
} ch_kv_pair;

// clang-format off
#define CH_KV_SINGLE(name, msgpack_type) {.key_name = name, .allowed_types = {msgpack_type}, .n_allowed = 1}
#define CH_KV_EITHER(name, msgpack_type1, msgpack_type2) {.key_name = name, .allowed_types = {msgpack_type1, msgpack_type2}, .n_allowed = 2}
#define CH_KV_WILD(name) {.key_name = name, .n_allowed = 0}
// clang-format on

typedef struct ch_kv_schema {
    size_t n_kv_pairs;
    const ch_kv_pair* kv_pairs;
} ch_kv_schema;

#define CH_DEFINE_KV_SCHEMA(schema_name, ...)                      \
    static const ch_kv_pair schema_name##_pairs[] = {__VA_ARGS__}; \
    static const ch_kv_schema schema_name = {                      \
        .n_kv_pairs = ARRAYSIZE(schema_name##_pairs),              \
        .kv_pairs = schema_name##_pairs,                           \
    };

#define CH_CHECK(expr)               \
    do {                             \
        ch_unpack_result res = expr; \
        if (res != CH_UNPACK_OK)     \
            return res;              \
    } while (0)

ch_unpack_result ch_check_kv_schema(msgpack_object o, ch_kv_schema schema)
{
    CH_CHECK_FORMAT(o.type == MSGPACK_OBJECT_MAP);
    CH_CHECK_FORMAT(o.via.map.size == schema.n_kv_pairs);
    const msgpack_object_kv* kv = o.via.map.ptr;
    for (size_t i = 0; i < schema.n_kv_pairs; i++) {
        CH_CHECK_STR_FORMAT(kv[i].key, schema.kv_pairs[i].key_name);
        if (schema.kv_pairs[i].n_allowed) {
            bool any = false;
            for (size_t j = 0; j < schema.kv_pairs[i].n_allowed; j++)
                any |= kv[i].val.type == schema.kv_pairs[i].allowed_types[j];
            CH_CHECK_FORMAT(any);
        }
    }
    return CH_UNPACK_OK;
}

static ch_unpack_result ch_recurse_visit_datamaps(const msgpack_object o,
                                                  ch_unpack_result (*cb)(const msgpack_object* o, void* user_data),
                                                  void* user_data)
{
    if (o.type == MSGPACK_OBJECT_NIL)
        return CH_UNPACK_OK;
    CH_CHECK(cb(&o, user_data));
    CH_CHECK(ch_recurse_visit_datamaps(o.via.map.ptr[CH_KV_IDX_DM_BASE].val, cb, user_data));
    msgpack_object_array fields = o.via.map.ptr[CH_KV_IDX_DM_FIELDS].val.via.array;
    for (size_t i = 0; i < fields.size; i++)
        CH_CHECK(ch_recurse_visit_datamaps(fields.ptr[i].via.map.ptr[CH_KV_IDX_TD_EMBEDDED].val, cb, user_data));
    return CH_UNPACK_OK;
}

static ch_unpack_result ch_check_dm_schema_cb(const msgpack_object* o, void* user_data)
{
    (void)user_data;
    CH_DEFINE_KV_SCHEMA(dm_kv_schema,
                        CH_KV_SINGLE(CHMPK_MSG_DM_NAME, MSGPACK_OBJECT_STR),
                        CH_KV_SINGLE(CHMPK_MSG_DM_MODULE, MSGPACK_OBJECT_STR),
                        CH_KV_SINGLE(CHMPK_MSG_DM_MODULE_OFF, MSGPACK_OBJECT_POSITIVE_INTEGER),
                        CH_KV_EITHER(CHMPK_MSG_DM_BASE, MSGPACK_OBJECT_MAP, MSGPACK_OBJECT_NIL),
                        CH_KV_SINGLE(CHMPK_MSG_DM_FIELDS, MSGPACK_OBJECT_ARRAY));
    CH_CHECK(ch_check_kv_schema(*o, dm_kv_schema));

    msgpack_object_array fields = o->via.map.ptr[CH_KV_IDX_DM_FIELDS].val.via.array;

    for (size_t i = 0; i < fields.size; i++) {
        CH_DEFINE_KV_SCHEMA(dm_td_schema,
                            CH_KV_SINGLE(CHMPK_MSG_TD_NAME, MSGPACK_OBJECT_STR),
                            CH_KV_SINGLE(CHMPK_MSG_TD_TYPE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_FLAGS, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_OFF, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_TOTAL_SIZE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_EITHER(CHMPK_MSG_TD_RESTORE_OPS, MSGPACK_OBJECT_POSITIVE_INTEGER, MSGPACK_OBJECT_NIL),
                            CH_KV_EITHER(CHMPK_MSG_TD_INPUT_FUNC, MSGPACK_OBJECT_POSITIVE_INTEGER, MSGPACK_OBJECT_NIL),
                            CH_KV_EITHER(CHMPK_MSG_TD_EMBEDDED, MSGPACK_OBJECT_MAP, MSGPACK_OBJECT_NIL),
                            CH_KV_SINGLE(CHMPK_MSG_TD_OVERRIDE_COUNT, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CHMPK_MSG_TD_TOL, MSGPACK_OBJECT_FLOAT32));
        CH_CHECK(ch_check_kv_schema(fields.ptr[i], dm_td_schema));
    }
    return CH_UNPACK_OK;
}

typedef struct ch_verify_and_hash_dm_cb_udata {
    ch_process_msg_ctx* ctx;
    bool* save_unpacked;
} ch_verify_and_hash_dm_cb_udata;

static ch_unpack_result ch_verify_and_hash_dm_cb(const msgpack_object* o, void* user_data)
{
    ch_verify_and_hash_dm_cb_udata* udata = (ch_verify_and_hash_dm_cb_udata*)user_data;
    msgpack_object_map dm = o->via.map;
    ch_hashmap_entry entry = {.name = dm.ptr[CH_KV_IDX_DM_NAME].val.via.str, .o = *o};
    uint64_t hash = ch_hashmap_entry_hash(&entry, 0, 0);
    const ch_hashmap_entry* existing = hashmap_get_with_hash(udata->ctx->dm_hashmap, &entry, hash);

    if (!existing) {
        hashmap_set_with_hash(udata->ctx->dm_hashmap, &entry, hash);
        if (hashmap_oom(udata->ctx->dm_hashmap))
            return CH_UNPACK_OUT_OF_MEMORY;
        *udata->save_unpacked = true;
        return CH_UNPACK_OK;
    }
    // I was originally going to do a deep comparison of all of the fields, but I can just compare the datamap pointers :)
    msgpack_object_map dm2 = existing->o.via.map;
    if (msgpack_object_equal(dm.ptr[CH_KV_IDX_DM_MODULE].val, dm2.ptr[CH_KV_IDX_DM_MODULE].val) &&
        msgpack_object_equal(dm.ptr[CH_KV_IDX_DM_MODULE_OFF].val, dm2.ptr[CH_KV_IDX_DM_MODULE_OFF].val))
        return CH_UNPACK_OK;

    msgpack_object_str name = dm.ptr[CH_KV_IDX_DM_NAME].val.via.str;
    ch_game_module mod_idx = dm.ptr[CH_KV_IDX_DM_MODULE].val.via.u64;
    CH_LOG_ERROR(udata->ctx,
                 "Two datamaps found with the same name '%.*s' (%s), but different type descriptions!",
                 name.size,
                 name.ptr,
                 ch_mod_names[mod_idx]);
    return CH_UNPACK_CONSTRAINT_FAIL;
}

static ch_unpack_result ch_count_maps_cb(const msgpack_object* o, void* user_data)
{
    (void)o;
    ++*(uint32_t*)user_data;
    return CH_UNPACK_OK;
}

typedef struct ch_map_sort_key {
    msgpack_object_map dm;
    uint32_t n_dependencies;
} ch_map_sort_key;

static int ch_cmp_by_n_dependencies(const ch_map_sort_key* a, const ch_map_sort_key* b)
{
    if (a->n_dependencies < b->n_dependencies)
        return -1;
    if (a->n_dependencies > b->n_dependencies)
        return 1;
    return ch_cmp_mp_str(a->dm.ptr[CH_KV_IDX_DM_NAME].val.via.str, b->dm.ptr[CH_KV_IDX_DM_NAME].val.via.str);
}

static void ch_change_datamap_references_to_strings(msgpack_object_map dm)
{
    // base map
    if (dm.ptr[CH_KV_IDX_DM_BASE].val.type == MSGPACK_OBJECT_MAP)
        dm.ptr[CH_KV_IDX_DM_BASE].val = dm.ptr[CH_KV_IDX_DM_BASE].val.via.map.ptr[CH_KV_IDX_DM_NAME].val;
    //embedded maps
    msgpack_object_array fields = dm.ptr[CH_KV_IDX_DM_FIELDS].val.via.array;
    for (size_t i = 0; i < fields.size; i++) {
        msgpack_object_map td = fields.ptr[i].via.map;
        if (td.ptr[CH_KV_IDX_TD_EMBEDDED].val.type == MSGPACK_OBJECT_MAP)
            td.ptr[CH_KV_IDX_TD_EMBEDDED].val = td.ptr[CH_KV_IDX_TD_EMBEDDED].val.via.map.ptr[CH_KV_IDX_DM_NAME].val;
    }
}

static void ch_write_all_to_file(ch_process_msg_ctx* ctx)
{
    // TODO add checks here to see if we receieved any data
    size_t n_datamaps = hashmap_count(ctx->dm_hashmap);
    ch_map_sort_key* maps = malloc(n_datamaps * sizeof(ch_map_sort_key));
    if (!maps) {
        CH_LOG_ERROR(ctx, "Out of memory (write_to_file).");
        return;
    }

    size_t map_idx = 0;
    size_t it = 0;
    ch_hashmap_entry* entry = NULL;
    while (hashmap_iter(ctx->dm_hashmap, &it, &entry)) {
        maps[map_idx].dm = entry->o.via.map;
        maps[map_idx].n_dependencies = 0;
        ch_recurse_visit_datamaps(entry->o, ch_count_maps_cb, &maps[map_idx].n_dependencies);
        map_idx++;
    }

    qsort(maps, n_datamaps, sizeof *maps, ch_cmp_by_n_dependencies);

    FILE* f = fopen(ctx->output_file_path, "wb");
    if (!f) {
        CH_LOG_ERROR(ctx, "Failed to write to file '%s', (errno=%d)", ctx->output_file_path, errno);
        return;
    }
    CH_LOG_INFO(ctx, "Writing %zu datamaps to '%s'.\n", n_datamaps, ctx->output_file_path);
    msgpack_packer packer = {.data = f, .callback = msgpack_fbuffer_write};

    msgpack_pack_array(&packer, n_datamaps);
    for (size_t i = 0; i < n_datamaps; i++) {
        ch_change_datamap_references_to_strings(maps[i].dm);
        msgpack_object o = {.type = MSGPACK_OBJECT_MAP, .via.map = maps[i].dm};
        if (msgpack_pack_object(&packer, o))
            CH_LOG_ERROR(ctx, "Failed writing datamaps (errno=%d).", errno);
    }
    free(maps);
    fclose(f);
}

static ch_unpack_result ch_process_msg(ch_process_msg_ctx* ctx, msgpack_object o, bool* save_unpacked)
{
    CH_DEFINE_KV_SCHEMA(msg_kv_schema,
                        CH_KV_SINGLE(CHMPK_MSG_TYPE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                        CH_KV_WILD(CHMPK_MSG_DATA));
    CH_CHECK(ch_check_kv_schema(o, msg_kv_schema));

    const msgpack_object_kv* kv = o.via.map.ptr;

    ch_comm_msg_type msg_type = kv[0].val.via.u64;
    msgpack_object msg_data = kv[1].val;
    if (msg_type != CH_MSG_HELLO && !ctx->got_hello) {
        CH_LOG_ERROR(ctx, "Payload sent other messages before sending HELLO.");
        return CH_UNPACK_CONSTRAINT_FAIL;
    }
    switch (msg_type) {
        case CH_MSG_HELLO:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_NIL);
            CH_LOG_INFO(ctx, "Got HELLO message from payload.\n");
            ctx->got_hello = true;
            return CH_UNPACK_OK;
        case CH_MSG_GOODBYE:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_NIL);
            CH_LOG_INFO(ctx, "Got GOODBYE message from payload.\n");
            ch_write_all_to_file(ctx);
            return CH_UNPACK_EXIT;
        case CH_MSG_LOG_INFO:
        case CH_MSG_LOG_ERROR:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_STR);
            ch_log_level level = msg_type == CH_MSG_LOG_INFO ? CH_LL_INFO : CH_LL_ERROR;
            CH_LOG_LEVEL(level, ctx, "[payload] %.*s\n", msg_data.via.str.size, msg_data.via.str.ptr);
            return CH_UNPACK_OK;
        case CH_MSG_DATAMAP:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_MAP);
            CH_CHECK(ch_recurse_visit_datamaps(msg_data, ch_check_dm_schema_cb, NULL));
            ch_verify_and_hash_dm_cb_udata udata = {.ctx = ctx, .save_unpacked = save_unpacked};
            CH_CHECK(ch_recurse_visit_datamaps(msg_data, ch_verify_and_hash_dm_cb, &udata));
            CH_LOG_INFO(ctx,
                        "Received datamap %.*s.\n",
                        msg_data.via.map.ptr[CH_KV_IDX_DM_NAME].val.via.str.size,
                        msg_data.via.map.ptr[CH_KV_IDX_DM_NAME].val.via.str.ptr);
            return CH_UNPACK_OK;
            break;
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
    CH_LOG_INFO(ctx, "[exe] recv msg (%zu bytes) ", ctx->msg_len);
    msgpack_object_print(stdout, ctx->unpacked_ll->unp.data);
    fprintf(stdout, "\n");
#endif

    bool save_unpacked = false;

    switch (ch_process_msg(ctx, ctx->unpacked_ll->unp.data, &save_unpacked)) {
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

    if (save_unpacked) {
        ch_mp_unpacked_ll* head = calloc(1, sizeof(ch_mp_unpacked_ll));
        if (!head) {
            CH_LOG_ERROR(ctx, "Out of memory.");
            return false;
        }
        head->next = ctx->unpacked_ll;
        ctx->unpacked_ll = head;
    } else {
        msgpack_unpacked_destroy(&ctx->unpacked_ll->unp);
    }

    ctx->msg_len = 0;
    return true;
}
