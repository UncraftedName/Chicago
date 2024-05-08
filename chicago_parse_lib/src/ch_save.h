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
#define CH_FOREACH_ERR(GEN)           \
    GEN(CH_ERR_NONE)                  \
                                      \
    /* generic errors */              \
    GEN(CH_ERR_FAILED_TO_READ_FILE)   \
    GEN(CH_ERR_OUT_OF_MEMORY)         \
    GEN(CH_ERR_READER_OVERFLOWED)     \
    GEN(CH_ERR_WRITER_OVERFLOWED)     \
    GEN(CH_ERR_BAD_SYMBOL_TABLE)      \
    GEN(CH_ERR_DATAMAP_NOT_FOUND)     \
                                      \
    /* .sav header errors */          \
    GEN(CH_ERR_SAV_BAD_TAG)           \
                                      \
    /* errors for field parsing */    \
    GEN(CH_ERR_BAD_FIELDS_MARKER)     \
    GEN(CH_ERR_BAD_SYMBOL)            \
    GEN(CH_ERR_BAD_FIELD_COUNT)       \
    GEN(CH_ERR_FIELD_NOT_FOUND)       \
    GEN(CH_ERR_BAD_FIELD_TYPE)        \
    GEN(CH_ERR_BAD_BLOCK_START)       \
    GEN(CH_ERR_BAD_BLOCK_END)         \
                                      \
    /* state file errors */           \
    GEN(CH_ERR_BAD_STATE_FILE_LENGTH) \
    GEN(CC_ERR_BAD_STATE_FILE_COUNT)  \
    GEN(CC_ERR_BAD_STATE_FILE_NAME)   \
                                      \
    /* .hl1 errors */                 \
    GEN(CC_ERR_HL1_BAD_TAG)           \
                                      \
    /* .hl2 errors */                 \
    GEN(CC_ERR_HL2_BAD_TAG)           \
    GEN(CC_ERR_HL2_BAD_SECTION_HEADER)

typedef enum ch_err { CH_FOREACH_ERR(CH_GENERATE_ENUM) } ch_err;
static const char* const ch_err_strs[] = {CH_FOREACH_ERR(CH_GENERATE_STRING)};

typedef enum ch_state_file_type {
    CH_SF_INVALID = 0,
    CH_SF_SAVE_DATA,
    CH_SF_ADJACENT_CLIENT_STATE,
    CH_SF_ENTITY_PATCH,
} ch_state_file_type;

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


typedef struct ch_restored_entity {
    ch_restored_class class_info;
    // TODO these are more complicated than just simple restored classes, make special structs for them
    // TODO should be possible to check the vtable of entities and check if there's some custom restore funcs, probably too much effort...
    // TODO liquid portals lololol
    ch_restored_class ai_header; // only if the entity inherits from CAI_BaseNPC
    ch_restored_class speaker_info; // only if the entity inherits from CSpeaker
} ch_restored_entity;

typedef struct ch_block_entities {
    ch_restored_class_arr entity_table;
    ch_restored_entity* entities; // has entity_table.n_elems entities
} ch_block_entities;

typedef struct ch_sf_save_data {
    ch_tag tag;
    ch_restored_class save_header;
    ch_restored_class_arr adjacent_levels;
    ch_restored_class_arr light_styles;
    struct {
        ch_block_entities entities;
    } blocks;
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
    ch_state_file_type sf_type;
    union {
        ch_sf_save_data sf_save_data;
        ch_sf_adjacent_client_state sf_adjacent_client_state;
        ch_sf_entity_patch sf_entity_patch;
    };
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

typedef struct ch_parse_save_error {
    struct ch_parse_save_error* next;
    const char* err_str;
} ch_parse_save_error;

// make sure to use ch_parsed_save_new to create this class :)
typedef struct ch_parsed_save_data {
    ch_tag tag;
    ch_state_file* state_files;
    size_t n_state_files;
    ch_restored_class game_header;
    ch_restored_class global_state;
    ch_parse_save_error* errors_ll;
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

#define CH_FIELD_AT_PTR(restored_data, td_ptr, c_type) ((c_type*)((restored_data) + (td_ptr)->ch_offset))
#define CH_FIELD_AT(restored_data, td_ptr, c_type) (*CH_FIELD_AT_PTR(restored_data, td_ptr, c_type))
