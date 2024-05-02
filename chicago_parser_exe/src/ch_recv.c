#include <stdio.h>
#include <errno.h>

#include "ch_recv.h"
#include "SDK/datamap.h"
#include "thirdparty/hashmap/hashmap.h"

#include "thirdparty/msgpack/include/ch_msgpack.h"
#include "thirdparty/msgpack/include/msgpack/fbuffer.h"

#include "ch_save.h"
#include "ch_archive.h"
#include "ch_arena.h" // just for ch_align_to

typedef struct ch_mp_unpacked_ll {
    msgpack_unpacked unp;
    struct ch_mp_unpacked_ll* next;
    char _pad[4];
} ch_mp_unpacked_ll;

typedef struct ch_process_msg_ctx {
    bool got_hello;
    char _pad[3];
    ch_log_level log_level;

    /*
    * We use a single unpacker and unpack each message into a single unpacked structure.
    * If the datamap is one that we've already receieved (e.g. we were sent a CHL2_Player
    * map and then a CBaseEntity map later), then we reuse the unpacked object. Otherwise,
    * we keep the object alive in this linked list.
    */
    msgpack_unpacker mp_unpacker;
    ch_mp_unpacked_ll* unpacked_ll;
    // how much we'll expand the msgpack buffer on next fail
    size_t buf_expand_size;
    // total length of current message over IPC, only used for debugging
    size_t msg_len;

    /*
    * We'll store datamap names to the corresponding datamap object for all datamaps. When
    * we get a new datamap we'll store the map itself as well as any new base/embedded sorted_maps.
    * If we get a datamap that we already have, then we'll verify that it's equal to the
    * one we got before.
    */
    struct hashmap* dm_hashmap;

    const ch_datamap_collection_info* collection_save_info;
} ch_process_msg_ctx;

typedef struct ch_hashmap_entry {
    // key
    msgpack_object_str name;

    // values

    msgpack_object o;
    // these are used later when writing to file
    size_t n_dependencies;
    size_t offset;
} ch_hashmap_entry;

static uint64_t ch_hashmap_entry_hash(const void* key, uint64_t seed0, uint64_t seed1)
{
    const ch_hashmap_entry* entry = key;
    return hashmap_xxhash3(entry->name.ptr, entry->name.size, seed0, seed1);
}

static inline int ch_cmp_mp_str(msgpack_object_str s1, msgpack_object_str s2)
{
    // I'd like strnicmp, but in theory that can break mega-hard if two sorted_maps have the same lowercase name
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

ch_process_msg_ctx* ch_msg_ctx_alloc(ch_log_level log_level,
                                     size_t init_chunk_size,
                                     const ch_datamap_collection_info* collection_save_info)
{
    ch_process_msg_ctx* ctx = calloc(1, sizeof(ch_process_msg_ctx));
    if (!ctx)
        return NULL;
    ctx->log_level = log_level;
    ctx->collection_save_info = collection_save_info;
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

typedef enum ch_process_result {
    // all is well
    CH_PROCESS_OK,
    // got goodbye, we can stop now
    CH_PROCESS_FINISHED,
    // callee reports error
    CH_PROCESS_ERROR,
    // caller reports error
    CH_PROCESS_OUT_OF_MEMORY,
    CH_PROCESS_BAD_FORMAT,
} ch_process_result;

#define CH_CHECK_FORMAT(cond)             \
    do {                                  \
        if (!(cond)) {                    \
            assert(0);                    \
            return CH_PROCESS_BAD_FORMAT; \
        }                                 \
    } while (0)

#define CH_CHECK_STR_FORMAT(mp_str_obj, comp_str)                                                   \
    do {                                                                                            \
        CH_CHECK_FORMAT((mp_str_obj).type == MSGPACK_OBJECT_STR);                                   \
        CH_CHECK_FORMAT(!strncmp((mp_str_obj).via.str.ptr, comp_str, ((mp_str_obj).via.str).size)); \
    } while (0)

// schema stuff that's used for verifying the receieved format

typedef struct ch_kv_pair {
    const char* key_name;
    const uint8_t allowed_types[2];
    uint8_t n_allowed;
    uint8_t _pad;
} ch_kv_pair;

// clang-format off
#define CH_KV_SINGLE(name, msgpack_type) {.key_name = CH_KEY_NAME(name), .allowed_types = {msgpack_type}, .n_allowed = 1}
#define CH_KV_EITHER(name, msgpack_type1, msgpack_type2) {.key_name = CH_KEY_NAME(name), .allowed_types = {msgpack_type1, msgpack_type2}, .n_allowed = 2}
#define CH_KV_WILD(name) {.key_name = CH_KEY_NAME(name), .n_allowed = 0}
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

#define CH_CHECK(expr)                \
    do {                              \
        ch_process_result res = expr; \
        if (res != CH_PROCESS_OK)     \
            return res;               \
    } while (0)

ch_process_result ch_check_kv_schema(msgpack_object o, ch_kv_schema schema)
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
    return CH_PROCESS_OK;
}

// visit this datamap and base/embedded sorted_maps
static ch_process_result ch_recurse_visit_datamaps(const msgpack_object o,
                                                   ch_process_result (*cb)(const msgpack_object* o, void* user_data),
                                                   void* user_data)
{
    if (o.type == MSGPACK_OBJECT_NIL)
        return CH_PROCESS_OK;
    CH_CHECK(cb(&o, user_data));
    CH_CHECK(ch_recurse_visit_datamaps(o.via.map.ptr[CH_DM_BASE].val, cb, user_data));
    msgpack_object_array fields = o.via.map.ptr[CH_DM_FIELDS].val.via.array;
    for (size_t i = 0; i < fields.size; i++)
        CH_CHECK(ch_recurse_visit_datamaps(fields.ptr[i].via.map.ptr[CH_TD_EMBEDDED].val, cb, user_data));
    return CH_PROCESS_OK;
}

static ch_process_result ch_check_dm_schema_cb(const msgpack_object* o, void* user_data)
{
    (void)user_data;
    CH_DEFINE_KV_SCHEMA(dm_kv_schema,
                        CH_KV_SINGLE(CH_DM_NAME, MSGPACK_OBJECT_STR),
                        CH_KV_SINGLE(CH_DM_MODULE, MSGPACK_OBJECT_STR),
                        CH_KV_SINGLE(CH_DM_MODULE_OFF, MSGPACK_OBJECT_POSITIVE_INTEGER),
                        CH_KV_EITHER(CH_DM_BASE, MSGPACK_OBJECT_MAP, MSGPACK_OBJECT_NIL),
                        CH_KV_SINGLE(CH_DM_FIELDS, MSGPACK_OBJECT_ARRAY));
    CH_CHECK(ch_check_kv_schema(*o, dm_kv_schema));

    msgpack_object_array fields = o->via.map.ptr[CH_DM_FIELDS].val.via.array;

    for (size_t i = 0; i < fields.size; i++) {
        CH_DEFINE_KV_SCHEMA(dm_td_schema,
                            CH_KV_SINGLE(CH_TD_NAME, MSGPACK_OBJECT_STR),
                            CH_KV_SINGLE(CH_TD_TYPE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CH_TD_FLAGS, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_EITHER(CH_TD_EXTERNAL_NAME, MSGPACK_OBJECT_STR, MSGPACK_OBJECT_NIL),
                            CH_KV_SINGLE(CH_TD_OFF, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CH_TD_NUM_ELEMS, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CH_TD_TOTAL_SIZE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_EITHER(CH_TD_RESTORE_OPS, MSGPACK_OBJECT_POSITIVE_INTEGER, MSGPACK_OBJECT_NIL),
                            CH_KV_EITHER(CH_TD_INPUT_FUNC, MSGPACK_OBJECT_POSITIVE_INTEGER, MSGPACK_OBJECT_NIL),
                            CH_KV_EITHER(CH_TD_EMBEDDED, MSGPACK_OBJECT_MAP, MSGPACK_OBJECT_NIL),
                            CH_KV_SINGLE(CH_TD_OVERRIDE_COUNT, MSGPACK_OBJECT_POSITIVE_INTEGER),
                            CH_KV_SINGLE(CH_TD_TOL, MSGPACK_OBJECT_FLOAT32));
        CH_CHECK(ch_check_kv_schema(fields.ptr[i], dm_td_schema));
    }
    return CH_PROCESS_OK;
}

typedef struct ch_verify_and_hash_dm_cb_udata {
    ch_process_msg_ctx* ctx;
    bool* save_unpacked;
} ch_verify_and_hash_dm_cb_udata;

// add the datamap into the hashmap if it's new, compare with an existing one if it's not
static ch_process_result ch_verify_and_hash_dm_cb(const msgpack_object* o, void* user_data)
{
    ch_verify_and_hash_dm_cb_udata* udata = (ch_verify_and_hash_dm_cb_udata*)user_data;
    msgpack_object_map dm = o->via.map;
    ch_hashmap_entry entry = {.name = dm.ptr[CH_DM_NAME].val.via.str, .o = *o};
    uint64_t hash = ch_hashmap_entry_hash(&entry, 0, 0);
    const ch_hashmap_entry* existing = hashmap_get_with_hash(udata->ctx->dm_hashmap, &entry, hash);

    if (!existing) {
        hashmap_set_with_hash(udata->ctx->dm_hashmap, &entry, hash);
        if (hashmap_oom(udata->ctx->dm_hashmap))
            return CH_PROCESS_OUT_OF_MEMORY;
        *udata->save_unpacked = true;
        return CH_PROCESS_OK;
    }
    // I was originally going to do a deep comparison of all of the fields, but I can just compare the datamap pointers :)
    msgpack_object_map dm2 = existing->o.via.map;
    if (msgpack_object_equal(dm.ptr[CH_DM_MODULE].val, dm2.ptr[CH_DM_MODULE].val) &&
        msgpack_object_equal(dm.ptr[CH_DM_MODULE_OFF].val, dm2.ptr[CH_DM_MODULE_OFF].val))
        return CH_PROCESS_OK;

    msgpack_object_str name = dm.ptr[CH_DM_NAME].val.via.str;
    msgpack_object_str mod_name = dm.ptr[CH_DM_MODULE].val.via.str;
    CH_LOG_ERROR(udata->ctx,
                 "Two datamaps found with the same name '%.*s' in %.*s, but different type descriptions!",
                 name.size,
                 name.ptr,
                 mod_name.size,
                 mod_name.ptr);
    return CH_PROCESS_ERROR;
}

static ch_process_result ch_count_maps_cb(const msgpack_object* o, void* user_data)
{
    (void)o;
    ++*(uint32_t*)user_data;
    return CH_PROCESS_OK;
}

static int ch_cmp_datamaps(const ch_hashmap_entry** a, const ch_hashmap_entry** b)
{
    int cmp =
        ch_cmp_mp_str((**a).o.via.map.ptr[CH_DM_MODULE].val.via.str, (**b).o.via.map.ptr[CH_DM_MODULE].val.via.str);
    if (cmp)
        return cmp;
    if ((**a).n_dependencies < (**b).n_dependencies)
        return -1;
    if ((**a).n_dependencies > (**b).n_dependencies)
        return 1;
    return ch_cmp_mp_str((**a).o.via.map.ptr[CH_DM_NAME].val.via.str, (**b).o.via.map.ptr[CH_DM_NAME].val.via.str);
}

// change the msgpack objects which reference base/embedded sorted_maps to just strings
static void ch_change_datamap_references_to_strings(msgpack_object_map dm)
{
    // base map
    if (dm.ptr[CH_DM_BASE].val.type == MSGPACK_OBJECT_MAP)
        dm.ptr[CH_DM_BASE].val = dm.ptr[CH_DM_BASE].val.via.map.ptr[CH_DM_NAME].val;
    // embedded maps
    msgpack_object_array fields = dm.ptr[CH_DM_FIELDS].val.via.array;
    for (size_t i = 0; i < fields.size; i++) {
        msgpack_object_map td = fields.ptr[i].via.map;
        if (td.ptr[CH_TD_EMBEDDED].val.type == MSGPACK_OBJECT_MAP)
            td.ptr[CH_TD_EMBEDDED].val = td.ptr[CH_TD_EMBEDDED].val.via.map.ptr[CH_DM_NAME].val;
    }
}

static ch_process_result ch_msgpack_write_collection(msgpack_packer* pk,
                                                     const ch_hashmap_entry** sorted_maps,
                                                     size_t n_sorted_maps,
                                                     const ch_datamap_collection_info* collection_save_info)
{
#define CH_TRY_MSGPACK(expr)         \
    do {                             \
        if (msgpack_pack_##expr)     \
            return CH_PROCESS_ERROR; \
    } while (0)

    CH_TRY_MSGPACK(map(pk, CH_KEY_GROUP_COUNT(KEYS_FILE_HEADER)));
    CH_TRY_MSGPACK(str_with_body(pk, CH_HEADER_VERSION_key, strlen(CH_HEADER_VERSION_key)));
    CH_TRY_MSGPACK(int(pk, CH_MSGPACK_FORMAT_VERSION));
    CH_TRY_MSGPACK(str_with_body(pk, CH_HEADER_GAME_NAME_key, strlen(CH_HEADER_GAME_NAME_key)));
    CH_TRY_MSGPACK(str_with_body(pk, collection_save_info->game_name, strlen(collection_save_info->game_name)));
    CH_TRY_MSGPACK(str_with_body(pk, CH_HEADER_GAME_VERSION_key, strlen(CH_HEADER_GAME_VERSION_key)));
    CH_TRY_MSGPACK(str_with_body(pk, collection_save_info->game_version, strlen(collection_save_info->game_version)));
    CH_TRY_MSGPACK(str_with_body(pk, CH_HEADER_DATAMAPS_key, strlen(CH_HEADER_DATAMAPS_key)));
    CH_TRY_MSGPACK(array(pk, n_sorted_maps));
    for (size_t i = 0; i < n_sorted_maps; i++) {
        ch_change_datamap_references_to_strings(sorted_maps[i]->o.via.map);
        msgpack_object o = {.type = MSGPACK_OBJECT_MAP, .via.map = sorted_maps[i]->o.via.map};
        CH_TRY_MSGPACK(object(pk, o));
    }
    return CH_PROCESS_OK;

#undef CH_TRY_MSGPACK
}

static ch_process_result ch_add_strings_to_map(struct hashmap* str_map,
                                               msgpack_object* strs,
                                               size_t n_strs,
                                               size_t* string_alloc_size)
{
    for (size_t i = 0; i < n_strs; i++) {
        if (strs[i].type == MSGPACK_OBJECT_NIL)
            continue;
        assert(strs[i].type == MSGPACK_OBJECT_STR);
        ch_hashmap_entry entry = {.name = strs[i].via.str, .offset = *string_alloc_size};
        uint64_t hash = ch_hashmap_entry_hash(&entry, 0, 0);
        if (!hashmap_get_with_hash(str_map, &entry, hash)) {
            hashmap_set_with_hash(str_map, &entry, hash);
            *string_alloc_size += entry.name.size + 1;
            if (hashmap_oom(str_map))
                return CH_PROCESS_OUT_OF_MEMORY;
        }
    }
    return CH_PROCESS_OK;
}

static size_t ch_get_entry_offset(struct hashmap* map, msgpack_object mp_str)
{
    if (mp_str.type == MSGPACK_OBJECT_NIL)
        return CH_REL_OFF_NULL;
    assert(mp_str.type == MSGPACK_OBJECT_STR);
    ch_hashmap_entry entry_in = {.name = mp_str.via.str};
    const ch_hashmap_entry* entry_out = hashmap_get(map, &entry_in);
    assert(entry_out);
    return entry_out->offset;
}

static ch_process_result ch_create_naked_packed_collection(ch_process_msg_ctx* ctx,
                                                           ch_hashmap_entry** sorted_maps,
                                                           size_t n_sorted_maps,
                                                           ch_byte_array* collection_out)
{

    ch_process_result result = CH_PROCESS_OK;

    // msgpack str -> index
    struct hashmap* hm_unique_strs = hashmap_new(sizeof(ch_hashmap_entry),
                                                 n_sorted_maps * 16,
                                                 0,
                                                 0,
                                                 ch_hashmap_entry_hash,
                                                 ch_hashmap_entry_compare,
                                                 NULL,
                                                 NULL);
    size_t total_typedescs_to_write = 0;
    size_t string_alloc_size = 0;

    // add all unique strings to hashmap, count how much space we need for them, & count type descs
    for (size_t i = 0; i < n_sorted_maps; i++) {
        msgpack_object_kv* dm_kv = sorted_maps[i]->o.via.map.ptr;
        msgpack_object dm_strs[] = {dm_kv[CH_DM_NAME].val, dm_kv[CH_DM_MODULE].val};
        result = ch_add_strings_to_map(hm_unique_strs, dm_strs, ARRAYSIZE(dm_strs), &string_alloc_size);
        if (result != CH_PROCESS_OK)
            goto end;
        msgpack_object_array fields = dm_kv[CH_DM_FIELDS].val.via.array;
        for (size_t j = 0; j < fields.size; j++) {
            msgpack_object_kv* td_kv = fields.ptr[j].via.map.ptr;
            if (!(td_kv[CH_TD_FLAGS].val.via.u64 & FTYPEDESC_SAVE))
                continue;
            total_typedescs_to_write++;
            msgpack_object td_strs[] = {td_kv[CH_TD_NAME].val, td_kv[CH_TD_EXTERNAL_NAME].val};
            result = ch_add_strings_to_map(hm_unique_strs, td_strs, ARRAYSIZE(td_strs), &string_alloc_size);
            if (result != CH_PROCESS_OK)
                goto end;
        }
        sorted_maps[i]->offset = i;
    }

    collection_out->len = sizeof(ch_datamap_collection) + n_sorted_maps * sizeof(ch_datamap) +
                          total_typedescs_to_write * sizeof(ch_type_description) + string_alloc_size +
                          sizeof(ch_datamap_collection_tag);

    collection_out->arr = calloc(1, collection_out->len);
    if (!collection_out->arr) {
        result = CH_PROCESS_OUT_OF_MEMORY;
        goto end;
    }

    ch_datamap_collection* ch_col = (ch_datamap_collection*)collection_out->arr;
    ch_datamap* ch_dms = (ch_datamap*)(ch_col + 1);
    ch_type_description* ch_tds = (ch_type_description*)(ch_dms + n_sorted_maps);
    char* string_buf = (char*)(ch_tds + total_typedescs_to_write);
    ch_datamap_collection_tag* ch_tag = (ch_datamap_collection_tag*)(string_buf + string_alloc_size);

    // fill collection
    ch_col->lookup = NULL;

    // fill strings
    ch_hashmap_entry* entry = NULL;
    size_t bucket = 0;
    while (hashmap_iter(hm_unique_strs, &bucket, &entry))
        memcpy(string_buf + entry->offset, entry->name.ptr, entry->name.size);

#ifndef NDEBUG
    // check that string section is filled (at most one '\0' char between consecutive strings)
    for (const char* s = string_buf; s < (char*)ch_tag - 2; s++)
        assert(*(uint16_t*)s != 0);
#endif

    // fill maps & type descriptions
    ch_datamap* ch_dm = ch_dms;
    ch_type_description* ch_td = ch_tds;
    for (size_t i = 0; i < n_sorted_maps; i++) {
        msgpack_object_map mp_dm = sorted_maps[i]->o.via.map;
        msgpack_object_array mp_tds = mp_dm.ptr[CH_DM_FIELDS].val.via.array;
        ch_change_datamap_references_to_strings(mp_dm);
        ch_dm->base_map_rel_off = ch_get_entry_offset(ctx->dm_hashmap, mp_dm.ptr[CH_DM_BASE].val);
        ch_dm->class_name_rel_off = ch_get_entry_offset(hm_unique_strs, mp_dm.ptr[CH_DM_NAME].val);
        ch_dm->module_name_rel_off = ch_get_entry_offset(hm_unique_strs, mp_dm.ptr[CH_DM_MODULE].val);
        ch_dm->fields_rel_off = (size_t)(ch_td - ch_tds);
        // packed offset will be relative to class start
        // maps are sorted by dependency order, so base dm will be processed first (if it exists)
        size_t ch_off = 0;
        if (ch_dm->base_map_rel_off != CH_REL_OFF_NULL)
            ch_off = ch_dms[ch_dm->base_map_rel_off].ch_size;
        size_t n_ch_dm_tds = 0;
        for (size_t j = 0; j < mp_tds.size; j++) {
            msgpack_object_kv* td_kv = mp_tds.ptr[j].via.map.ptr;
            if (!(td_kv[CH_TD_FLAGS].val.via.u64 & FTYPEDESC_SAVE))
                continue;
            n_ch_dm_tds++;
            ch_td->type = td_kv[CH_TD_TYPE].val.via.u64;
            ch_td->name_rel_off = ch_get_entry_offset(hm_unique_strs, td_kv[CH_TD_NAME].val);
            ch_td->external_name_rel_off = ch_get_entry_offset(hm_unique_strs, td_kv[CH_TD_EXTERNAL_NAME].val);
            ch_td->game_offset = (size_t)td_kv[CH_TD_OFF].val.via.u64;
            ch_td->ch_offset = ch_off;
            ch_td->total_size_bytes = (size_t)td_kv[CH_TD_TOTAL_SIZE].val.via.u64;
            ch_td->flags = (unsigned short)td_kv[CH_TD_FLAGS].val.via.u64;
            ch_td->n_elems = (unsigned short)td_kv[CH_TD_NUM_ELEMS].val.via.u64;
            ch_td->save_restore_ops_rel_off = (size_t)td_kv[CH_TD_RESTORE_OPS].val.via.u64;
            ch_td->embedded_map_rel_off = ch_get_entry_offset(ctx->dm_hashmap, td_kv[CH_TD_EMBEDDED].val);
            if (ch_td->type == FIELD_CUSTOM)
                ch_off += sizeof(void*);
            else
                ch_off += CH_ALIGN_TO(ch_td->total_size_bytes, min(sizeof(void*), ch_td->total_size_bytes));
            ch_td++;
        }
        ch_dm->n_fields = n_ch_dm_tds;
        if (n_ch_dm_tds == 0)
            ch_dm->fields_rel_off = CH_REL_OFF_NULL;
        ch_dm->ch_size = CH_ALIGN_TO(ch_off, sizeof(void*));
        ch_dm++;
    }

    // consistency checks, check that the expected amount of data was written
    assert((size_t)(ch_dm - ch_dms) == n_sorted_maps);
    assert((size_t)(ch_td - ch_tds) == total_typedescs_to_write);
    assert((void*)ch_dm == (void*)ch_tds);
    assert((void*)ch_td == (void*)string_buf);

    // fill tag
    ch_tag->n_datamaps = n_sorted_maps;
    ch_tag->datamaps_start = (size_t)ch_dms - (size_t)collection_out->arr;
    ch_tag->typedescs_start = (size_t)ch_tds - (size_t)collection_out->arr;
    ch_tag->strings_start = (size_t)string_buf - (size_t)collection_out->arr;
    ch_tag->version = CH_DATAMAP_STRUCT_VERSION;
    strncpy(ch_tag->magic, CH_COLLECTION_FILE_MAGIC, sizeof ch_tag->magic);

end:
    hashmap_free(hm_unique_strs);
    return result;
}

static ch_process_result ch_write_all_to_file(ch_process_msg_ctx* ctx)
{
    // TODO add checks here to see if we receieved any data
    size_t n_datamaps = hashmap_count(ctx->dm_hashmap);
    if (n_datamaps == 0) {
        CH_LOG_ERROR(ctx, "Writing to file without any sent datamaps, stopping.");
        return CH_PROCESS_ERROR;
    }

    ch_hashmap_entry** sorted_maps = malloc(n_datamaps * sizeof(ch_hashmap_entry*));

    if (!sorted_maps)
        return CH_PROCESS_OUT_OF_MEMORY;

    size_t map_idx = 0;
    size_t it = 0;
    while (hashmap_iter(ctx->dm_hashmap, &it, &sorted_maps[map_idx])) {
        sorted_maps[map_idx]->n_dependencies = 0;
        ch_recurse_visit_datamaps(sorted_maps[map_idx]->o, ch_count_maps_cb, &sorted_maps[map_idx]->n_dependencies);
        sorted_maps[map_idx]->offset = map_idx++;
    }

    /*
    * Sort by the number of base/embedded sorted_maps each map has and then by name. This guarantees that all the
    * dependencies of each map are before it in the list. The only exception would be if there was some circular
    * dependencies but that would very much go against how datamaps function.
    */
    qsort(sorted_maps, n_datamaps, sizeof *sorted_maps, ch_cmp_datamaps);

    ch_process_result result = CH_PROCESS_OK;

    switch (ctx->collection_save_info->output_type) {
        case CH_DC_STRUCT_MSGPACK: {
            FILE* f = fopen(ctx->collection_save_info->output_file_path, "wb");
            if (!f) {
                CH_LOG_ERROR(ctx,
                             "Failed to write to file '%s', (errno=%d)",
                             ctx->collection_save_info->output_file_path,
                             errno);
                result = CH_PROCESS_ERROR;
                goto end;
            }
            CH_LOG_INFO(ctx,
                        "Writing %zu datamaps to '%s'.\n",
                        n_datamaps,
                        ctx->collection_save_info->output_file_path);
            msgpack_packer packer = {.data = f, .callback = msgpack_fbuffer_write};
            result = ch_msgpack_write_collection(&packer, sorted_maps, n_datamaps, ctx->collection_save_info);
            if (result != CH_PROCESS_OK)
                CH_LOG_ERROR(ctx, "Failed writing datamaps (errno=%d).", errno);
            fclose(f);
            break;
        }
        case CH_DC_STRUCT_NAKED: {
            ch_byte_array collection;
            result = ch_create_naked_packed_collection(ctx, sorted_maps, n_datamaps, &collection);
            if (result != CH_PROCESS_OK)
                break;
            FILE* f = fopen(ctx->collection_save_info->output_file_path, "wb");
            if (!f) {
                CH_LOG_ERROR(ctx,
                             "Failed to write to file '%s', (errno=%d)",
                             ctx->collection_save_info->output_file_path,
                             errno);
                result = CH_PROCESS_ERROR;
                goto end;
            }
            CH_LOG_INFO(ctx,
                        "Writing %zu datamaps to '%s'.\n",
                        n_datamaps,
                        ctx->collection_save_info->output_file_path);
            size_t written = fwrite(collection.arr, 1, collection.len, f);
            if (written != collection.len) {
                CH_LOG_ERROR(ctx, "fwrite() failed");
                result = CH_PROCESS_ERROR;
            }
            fclose(f);
            ch_free_array(&collection);
            break;
        }
        case CH_DC_ARCHIVE_NAKED:
        default:
            assert(0);
    }

end:
    free(sorted_maps);
    return result;
}

// process the msgpack object sent by the payload
static ch_process_result ch_process_message_pack_msg(ch_process_msg_ctx* ctx, msgpack_object o, bool* save_unpacked)
{
    CH_DEFINE_KV_SCHEMA(msg_kv_schema,
                        CH_KV_SINGLE(CH_IPC_TYPE, MSGPACK_OBJECT_POSITIVE_INTEGER),
                        CH_KV_WILD(CH_IPC_DATA));
    CH_CHECK(ch_check_kv_schema(o, msg_kv_schema));

    const msgpack_object_kv* kv = o.via.map.ptr;

    ch_comm_msg_type msg_type = kv[0].val.via.u64;
    msgpack_object msg_data = kv[1].val;
    if (msg_type != CH_MSG_HELLO && !ctx->got_hello) {
        CH_LOG_ERROR(ctx, "Payload sent other messages before sending HELLO.");
        return CH_PROCESS_ERROR;
    }
    switch (msg_type) {
        case CH_MSG_HELLO:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_NIL);
            CH_LOG_INFO(ctx, "Got HELLO message from payload.\n");
            ctx->got_hello = true;
            return CH_PROCESS_OK;
        case CH_MSG_GOODBYE:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_NIL);
            CH_LOG_INFO(ctx, "Got GOODBYE message from payload.\n");
            CH_CHECK(ch_write_all_to_file(ctx));
            return CH_PROCESS_FINISHED;
        case CH_MSG_LOG_INFO:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_STR);
            CH_LOG_LEVEL(CH_LL_INFO, ctx, "[payload INFO] %.*s\n", msg_data.via.str.size, msg_data.via.str.ptr);
            return CH_PROCESS_OK;
        case CH_MSG_LOG_ERROR:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_STR);
            CH_LOG_LEVEL(CH_LL_ERROR, ctx, "[payload ERROR] %.*s\n", msg_data.via.str.size, msg_data.via.str.ptr);
            return CH_PROCESS_FINISHED;
        case CH_MSG_DATAMAP:
            CH_CHECK_FORMAT(msg_data.type == MSGPACK_OBJECT_MAP);
            CH_CHECK(ch_recurse_visit_datamaps(msg_data, ch_check_dm_schema_cb, NULL));
            ch_verify_and_hash_dm_cb_udata udata = {.ctx = ctx, .save_unpacked = save_unpacked};
            CH_CHECK(ch_recurse_visit_datamaps(msg_data, ch_verify_and_hash_dm_cb, &udata));
            CH_LOG_INFO(ctx,
                        "Received datamap %.*s from %.*s.\n",
                        msg_data.via.map.ptr[CH_DM_NAME].val.via.str.size,
                        msg_data.via.map.ptr[CH_DM_NAME].val.via.str.ptr,
                        msg_data.via.map.ptr[CH_DM_MODULE].val.via.str.size,
                        msg_data.via.map.ptr[CH_DM_MODULE].val.via.str.ptr);
            return CH_PROCESS_OK;
            break;
        case CH_MSG_LINKED_NAME:
        default:
            return CH_PROCESS_BAD_FORMAT;
    }
}

// unpack the buffered data into msgpack
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

    switch (ch_process_message_pack_msg(ctx, ctx->unpacked_ll->unp.data, &save_unpacked)) {
        case CH_PROCESS_OK:
            break;
        case CH_PROCESS_FINISHED:
        case CH_PROCESS_ERROR:
            return false;
        case CH_PROCESS_BAD_FORMAT:
            CH_LOG_ERROR(ctx, "Received wrong msgpack format from payload.");
            return false;
        case CH_PROCESS_OUT_OF_MEMORY:
            CH_LOG_ERROR(ctx, "Out of memory while processing message from payload.");
            return false;
        default:
            assert(0);
            return false;
    }

    // save the unpacked object if had a new datamap by adding it the the linked list, otherwise reuse it
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
