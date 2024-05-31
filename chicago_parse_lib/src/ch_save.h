#pragma once

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "SDK/datamap.h"
#include "thirdparty/hashmap/hashmap.h"
#include "ch_arena.h"

#define CH_SAVE_FILE_MAX_SIZE (1024 * 1024 * 32)

#define CH_GENERATE_ENUM(v) v,
#define CH_GENERATE_STRING(v) #v,

// TODO move this bad boy to a separate file so that the search lib only includes what's required
#define CH_FOREACH_ERR(GEN)               \
    GEN(CH_ERR_NONE)                      \
                                          \
    /* generic errors */                  \
    GEN(CH_ERR_READER_OVERFLOWED)         \
    GEN(CH_ERR_FAILED_TO_READ_FILE)       \
    GEN(CH_ERR_OUT_OF_MEMORY)             \
    GEN(CH_ERR_WRITER_OVERFLOWED)         \
    GEN(CH_ERR_BAD_SYMBOL_TABLE)          \
    GEN(CH_ERR_DATAMAP_NOT_FOUND)         \
                                          \
    /* custom field registration */       \
    GEN(CH_ERR_CUSTOM_FIELD_CONFLICT)     \
                                          \
    /* .sav header errors */              \
    GEN(CH_ERR_SAV_BAD_TAG)               \
                                          \
    /* errors for field parsing */        \
    GEN(CH_ERR_BAD_FIELDS_MARKER)         \
    GEN(CH_ERR_BAD_SYMBOL)                \
    GEN(CH_ERR_BAD_FIELD_COUNT)           \
    GEN(CH_ERR_FIELD_NOT_FOUND)           \
    GEN(CH_ERR_BAD_FIELD_TYPE)            \
    GEN(CH_ERR_BAD_BLOCK_START)           \
    GEN(CH_ERR_BAD_BLOCK_END)             \
    GEN(CH_ERR_BAD_FIELD_READ)            \
    GEN(CH_ERR_CUSTOM_FIELD_PARSE)        \
                                          \
    /* state file errors */               \
    GEN(CH_ERR_BAD_STATE_FILE_LENGTH)     \
    GEN(CC_ERR_BAD_STATE_FILE_COUNT)      \
    GEN(CC_ERR_BAD_STATE_FILE_NAME)       \
                                          \
    /* .hl1 errors */                     \
    GEN(CH_ERR_HL1_BAD_TAG)               \
    GEN(CH_ERR_UNSUPPORTED_BLOCK_VERSION) \
                                          \
    /* .hl2 errors */                     \
    GEN(CH_ERR_HL2_BAD_TAG)               \
    GEN(CH_ERR_HL2_BAD_SECTION_HEADER)    \
                                          \
    /* dump errors */                     \
    GEN(CH_ERR_FILE_IO)                   \
    GEN(CH_ERR_MSGPACK)                   \
    GEN(CH_ERR_FMT_TOO_LONG)              \
    GEN(CH_ERR_ENCODING)

typedef enum ch_err { CH_FOREACH_ERR(CH_GENERATE_ENUM) } ch_err;
static const char* const ch_err_strs[] = {CH_FOREACH_ERR(CH_GENERATE_STRING)};

#define CH_FOREACH_SF_TYPE(GEN)      \
    GEN(CH_SF_INVALID)               \
    GEN(CH_SF_SAVE_DATA)             \
    GEN(CH_SF_ADJACENT_CLIENT_STATE) \
    GEN(CH_SF_ENTITY_PATCH)

typedef enum ch_state_file_type { CH_FOREACH_SF_TYPE(CH_GENERATE_ENUM) } ch_state_file_type;
static const char* const ch_sf_type_strs[] = {CH_FOREACH_SF_TYPE(CH_GENERATE_STRING)};

typedef enum ch_block_type {
    CH_BLOCK_ENTITIES,
    CH_BLOCK_PHYSICS,
    CH_BLOCK_AI,
    CH_BLOCK_TEMPLATES,
    CH_BLOCK_RESPONSE_SYSTEM,
    CH_BLOCK_COMMENTARY,
    CH_BLOCK_EVENT_QUEUE,
    CH_BLOCK_ACHIEVEMENTS,
    CH_BLOCK_COUNT,
} ch_block_type;

typedef enum ch_dump_flags {
    /*
    * If set, will not dump fields which are all zero to text/msgpack (for a lot of fields this
    * means the data was not in the save file).
    */
    CH_DF_IGNORE_ZERO_FIELDS = 1,

    /*
    * If set, fields will be dumped sorted by offset in the game class, otherwise they are sorted
    * by the order they appear in the datamap. If false, duplicate fields names will be present if
    * they were present in the game's datamap. Otherwise, duplicate field names will be dropped if
    * making a text dump, but will still be present for the msgpack dump (since each saved field
    * will reference the typedescription from which it comes).
    */
    CH_DF_SORT_FIELDS_BY_OFFSET = 2,
} ch_dump_flags;

typedef struct ch_tag {
    char id[4];
    uint32_t version;
} ch_tag;

typedef struct ch_restored_class {
    const ch_datamap* dm;
    unsigned char* data;
} ch_restored_class;

typedef struct ch_restored_class_arr {
    const ch_datamap* dm;
    size_t n_elems;
    // array of restored classes continuously in memory
    // i.e. {data + dm->ch_size * n} will give the nth class
    unsigned char* data;
} ch_restored_class_arr;

// helper macros for ch_restored_class_arr
#define CH_RCA_ELEM_DATA(rca, n) (assert((size_t)n < (rca).n_elems), ((rca).data + (rca).dm->ch_size * (n)))
#define CH_RCA_DATA_SIZE(rca) ((rca).dm->ch_size * (rca).n_elems)

typedef struct ch_str_ll {
    char* str;
    struct ch_str_ll* next;
} ch_str_ll;

typedef struct ch_npc_schedule_conditions {
    ch_str_ll* conditions;
    ch_str_ll* custom_interrupts;
    ch_str_ll* pre_ignore;
    ch_str_ll* ignore;
} ch_npc_schedule_conditions;

typedef struct ch_npc_navigator {
    int16_t version;
    char _pad[2];
    struct ch_cr_utl_vector* path_vec;
} ch_npc_navigator;

typedef struct ch_custom_ent_restore_base_npc {
    ch_restored_class extended_header;
    // only if extended_header.version >= CH_HEADER_AI_FIRST_VERSION_WITH_NAVIGATOR_SAVE
    ch_npc_schedule_conditions* schedule_conditions;
    // only if extended_header.version >= CH_HEADER_AI_FIRST_VERSION_WITH_NAVIGATOR_SAVE
    ch_npc_navigator* navigator;
} ch_custom_ent_restore_base_npc;

typedef struct ch_custom_ent_restore_speaker {
    int x; // TODO
} ch_custom_ent_restore_speaker;

typedef struct ch_restored_entity {
    const char* classname;
    ch_restored_class class_info;
    ch_custom_ent_restore_base_npc* npc_header;  // only if the entity inherits from CAI_BaseNPC
    ch_custom_ent_restore_speaker* speaker_info; // only if the entity inherits from CSpeaker
} ch_restored_entity;

typedef struct ch_template_data {
    unsigned char* template_data; // type: ch_block_templates.dm_template
    char* name;
    char* map_data;
} ch_template_data;

// TODO rename all of these
#define CH_HEADER_AI_FIRST_VERSION_WITH_CONDITIONS 2
#define CH_HEADER_AI_FIRST_VERSION_WITH_NAVIGATOR_SAVE 5
#define CH_NAVIGATOR_SAVE_VERSION 1

typedef struct ch_block_entities {
    ch_restored_class_arr entity_table;
    ch_restored_entity** entities; // has entity_table.n_elems entities
} ch_block_entities;

typedef struct ch_block_physics {
    int _x;
} ch_block_physics;

typedef struct ch_block_ai {
    int _x;
} ch_block_ai;

#define CH_HEADER_TEMPLATE_SAVE_RESTORE_VERSION 1

typedef struct ch_block_templates {
    int16_t version;
    int16_t n_templates;
    const ch_datamap* dm_template;
    ch_template_data* templates;
    int32_t template_instance; // TODO what the hell is this?
} ch_block_templates;

typedef struct ch_block_response_system {
    int _x;
} ch_block_response_system;

typedef struct ch_block_commentary {
    int _x;
} ch_block_commentary;

#define CH_HEADER_EVENTQUEUE_SAVE_RESTORE_VERSION 1

typedef struct ch_block_event_queue {
    int16_t version;
    char _pad[2];
    ch_restored_class queue;
    ch_restored_class_arr events;
} ch_block_event_queue;

typedef struct ch_block_achievements {
    int _x;
} ch_block_achievements;

typedef struct ch_block {
    int16_t vec_idx; // index into block header vec + 1 (0 is invalid)
    bool header_parsed;
    bool body_parsed;
    // one of the ch_block_* structures depending on which index we're at in the array (not the vector)
    void* data;
} ch_block;

typedef struct ch_sf_save_data {
    ch_tag tag;
    ch_restored_class save_header;
    ch_restored_class_arr adjacent_levels;
    ch_restored_class_arr light_styles;
    struct ch_cr_utl_vector* block_headers;
    ch_block blocks[CH_BLOCK_COUNT];
} ch_sf_save_data;

typedef struct ch_sf_adjacent_client_state {
    ch_tag tag;
} ch_sf_adjacent_client_state;

typedef struct ch_sf_entity_patch {
    int32_t n_patched_ents;
    int32_t* patched_ents;
} ch_sf_entity_patch;

typedef struct ch_state_file {
    char name[260];
    ch_state_file_type type;
    void* data; // one of the ch_sf_* structures depending on the type
} ch_state_file;

typedef struct ch_datamap_lookup_entry {
    const char* name;
    const ch_datamap* datamap;
} ch_datamap_lookup_entry;

static int ch_datamap_collection_name_compare(const void* a, const void* b, void* udata)
{
    (void)udata;
    const ch_datamap_lookup_entry* ea = (const ch_datamap_lookup_entry*)a;
    const ch_datamap_lookup_entry* eb = (const ch_datamap_lookup_entry*)b;
    assert(ea->name && eb->name);
    return strcmp(ea->name, eb->name);
}

static uint64_t ch_datamap_collection_name_hash(const void* key, uint64_t seed0, uint64_t seed1)
{
    assert(*(char**)key);
    const ch_datamap_lookup_entry* e = (const ch_datamap_lookup_entry*)key;
    return hashmap_xxhash3(e->name, strlen(e->name), seed0, seed1);
}

typedef struct ch_datamap_collection {
    // const char* name -> ch_datamap, see the hash & compare functions above
    struct hashmap* lookup;
} ch_datamap_collection;

typedef struct ch_parse_info {
    const ch_datamap_collection* datamap_collection;
    void* bytes;
    size_t n_bytes;
} ch_parse_info;

// make sure to use ch_parsed_save_new to create this class :)
typedef struct ch_parsed_save_data {
    ch_tag tag;
    ch_restored_class game_header;
    ch_restored_class global_state;
    ch_state_file* state_files;
    size_t n_state_files;
    ch_str_ll* errors_ll;
    ch_arena* _arena;
} ch_parsed_save_data;

ch_parsed_save_data* ch_parsed_save_new(void);
void ch_parsed_save_free(ch_parsed_save_data* parsed_data);
ch_err ch_parse_save_bytes(ch_parsed_save_data* parsed_data, const ch_parse_info* info);

ch_err ch_find_field(const ch_datamap* dm,
                     const char* field_name,
                     bool recurse_base_classes,
                     const ch_type_description** field);

bool ch_dm_inherts_from(const ch_datamap* dm, const char* base_name);

ch_err ch_dump_sav_to_text(FILE* f, const ch_parsed_save_data* save_data, const char* indent_str, ch_dump_flags flags);

#define CH_FIELD_AT_PTR(restored_data, td_ptr, c_type) ((c_type*)((restored_data) + (td_ptr)->ch_offset))
#define CH_FIELD_AT(restored_data, td_ptr, c_type) (*CH_FIELD_AT_PTR(restored_data, td_ptr, c_type))
