#pragma once

#include <stdint.h>
#include <stdio.h>

#define FOREACH_CH_ERR(GEN)         \
    GEN(CH_ERR_NONE)                \
    GEN(CH_ERR_FAILED_TO_READ_FILE) \
    GEN(CH_ERR_OUT_OF_MEMORY)       \
    GEN(CH_ERR_READER_OVERFLOWED)   \
    GEN(CH_ERR_INVALID_HEADER)      \
    GEN(CH_ERR_INVALID_SYMBOL_TABLE)

#define CH_GENERATE_ENUM(v) v,
#define CH_GENERATE_STRING(v) #v,

typedef enum ch_err { FOREACH_CH_ERR(CH_GENERATE_ENUM) } ch_err;
static const char* ch_err_strs[] = {FOREACH_CH_ERR(CH_GENERATE_STRING)};

typedef enum ch_state_file_type {
    CH_SF_SAVE_GAME,
    CH_SF_CLIENT_STATE,
    CH_SF_ENTITY_PATCH,
} ch_state_file_type;

typedef struct ch_save_header {
    char id[4];       // "JSAV"
    uint32_t version; // 0x0073
    int32_t header_fields_size_bytes;
    int32_t symbol_count;
    int32_t symbol_table_size_bytes;
} ch_save_header;

typedef struct ch_state_file {
    struct ch_state_file* next;
    char id[4];
    char name[260];
    ch_state_file_type sf_type;
    void* sf_data;
} ch_state_file;

typedef struct ch_parsed_save_data {
    char game_id[32];
    ch_save_header header;
    ch_state_file* state_files;
} ch_parsed_save_data;

ch_err ch_parse_save_from_file(ch_parsed_save_data* parsed_data, const char* file_path);
ch_err ch_parse_save_from_bytes(ch_parsed_save_data* parsed_data, void* bytes, size_t n_bytes);
void ch_free_save_data(ch_parsed_save_data* parsed_data);
