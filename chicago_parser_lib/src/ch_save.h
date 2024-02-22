#pragma once

#include <stdint.h>
#include <stdio.h>

#define CH_GENERATE_ENUM(v) v,
#define CH_GENERATE_STRING(v) #v,

#define FOREACH_CH_ERR(GEN)                   \
    GEN(CH_ERR_NONE)                          \
                                              \
    /* generic errors */                      \
    GEN(CH_ERR_FAILED_TO_READ_FILE)           \
    GEN(CH_ERR_OUT_OF_MEMORY)                 \
    GEN(CH_ERR_READER_OVERFLOWED)             \
    GEN(CH_ERR_INVALID_SYMBOL_TABLE)          \
                                              \
    GEN(CH_ERR_HEADER_INVALID_TAG)            \
                                              \
    /* state file header errors */            \
    GEN(CH_ERR_INVALID_STATE_FILE_LENGTH)     \
    GEN(CC_ERR_INVALID_NUMBER_OF_STATE_FILES) \
    GEN(CC_ERR_INVALID_STATE_FILE_NAME)       \
                                              \
    /* .hl1 errors TODO REMOVE THIS*/         \
    GEN(CC_ERR_HL1_INVALID_TAG)               \
    /* .hl2 errors */                         \
    GEN(CC_ERR_HL2_INVALID_TAG)               \
    GEN(CC_ERR_HL2_INVALID_SECTION_HEADER)

typedef enum ch_err { FOREACH_CH_ERR(CH_GENERATE_ENUM) } ch_err;
static const char* const ch_err_strs[] = {FOREACH_CH_ERR(CH_GENERATE_STRING)};

typedef enum ch_state_file_type {
    CH_SF_SAVE_DATA,
    CH_SF_ADJACENT_CLIENT_STATE,
    CH_SF_ENTITY_PATCH,
} ch_state_file_type;

typedef struct ch_tag {
    char id[4];
    uint32_t version;
} ch_tag;

typedef struct ch_sf_save_data {
    ch_tag tag;
} ch_sf_save_data;

typedef struct ch_sf_adjacent_client_state {
    ch_tag tag;
} ch_sf_adjacent_client_state;

typedef struct ch_sf_entity_patch {
    int32_t n_patched_ents;
    int32_t *patched_ents;
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

typedef struct ch_parsed_save_data {
    // char game_id[32];
    ch_tag tag;
    ch_state_file* state_files;
} ch_parsed_save_data;

ch_err ch_parse_save_from_file(ch_parsed_save_data* parsed_data, const char* file_path);
ch_err ch_parse_save_from_bytes(ch_parsed_save_data* parsed_data, void* bytes, size_t n_bytes);
void ch_free_parsed_save_data(ch_parsed_save_data* parsed_data);
