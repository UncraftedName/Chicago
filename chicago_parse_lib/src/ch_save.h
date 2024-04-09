#pragma once

#include <stdint.h>
#include <stdio.h>

#include "SDK/datamap.h"

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
                                      \
    /* .sav header errors */          \
    GEN(CH_ERR_SAV_BAD_TAG)           \
                                      \
    /* errors for field parsing */    \
    GEN(CH_ERR_BAD_FIELDS_MARKER)     \
    GEN(CH_ERR_BAD_SYMBOL)            \
    GEN(CH_ERR_BAD_FIELD_COUNT)       \
    GEN(CH_ERR_FIELD_NOT_FOUND)       \
    GEN(CH_ERR_BAD_FIELD_BLOCK_SIZE)  \
    GEN(CH_ERR_BAD_FIELD_TYPE)        \
    GEN(CH_ERR_BAD_BLOCK_SIZE)        \
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

typedef struct ch_parsed_field_info {
    int field_idx;
    int data_off;
    // A negative data len means that although this field was present in the save file, there wasn't
    // enough information to parse it. This should only happen for custom fields or guessed datamaps.
    int data_len;
} ch_parsed_field_info;

typedef struct ch_parsed_fields {
    struct ch_parsed_fields* base_fields;
    const ch_datamap* map;
    unsigned char* packed_data;
    ch_parsed_field_info* packed_info;
    int n_packed_fields;
} ch_parsed_fields;

typedef struct ch_sf_save_data {
    ch_tag tag;
} ch_sf_save_data;

typedef struct ch_sf_adjacent_client_state {
    ch_tag tag;
} ch_sf_adjacent_client_state;

typedef struct ch_sf_entity_patch {
    int32_t n_patched_ents;
    int32_t* patched_ents;
} ch_sf_entity_patch;

typedef struct ch_state_file {
    struct ch_state_file* next;
    char name[260];
    ch_state_file_type sf_type;
    union {
        ch_sf_save_data sf_save_data;
        ch_sf_adjacent_client_state sf_adjacent_client_state;
        ch_sf_entity_patch sf_entity_patch;
    };
} ch_state_file;

typedef struct ch_datamap_collection {
    const ch_datamap* maps;
    size_t n_maps;
} ch_datamap_collection;

typedef struct ch_parse_params {
    ch_datamap_collection* collections;
    size_t n_collections;
} ch_parse_params;

typedef struct ch_parsed_save_data {
    // char game_id[32];
    ch_tag tag;
    ch_state_file* state_files;
} ch_parsed_save_data;

ch_err ch_parse_save_path(ch_parsed_save_data* parsed_data, const char* file_path);
ch_err ch_parse_save_bytes(ch_parsed_save_data* parsed_data, void* bytes, size_t n_bytes);
void ch_free_parsed_save_data(ch_parsed_save_data* parsed_data);
