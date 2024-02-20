#pragma once

#include <stdint.h>
#include <stdio.h>

typedef enum ch_err {
    CH_ERR_NONE = 0,
    CH_ERR_FAILED_TO_READ_FILE,
    CH_ERR_OEM,
} ch_err;

typedef enum ch_state_file_type {
    CH_SF_SAVE_GAME,
    CH_SF_CLIENT_STATE,
    CH_SF_ENTITY_PATCH,
} ch_state_file_type;

typedef unsigned short ch_symbol_offset;

typedef struct ch_parse_context {
    const char* symbol_table;
    ch_symbol_offset* symbol_offsets;
    ch_symbol_offset num_symbols;
} ch_parse_context;

typedef struct ch_save_header {
    char id[4];       // "JSAV"
    uint32_t version; // 0x0073
    uint32_t token_table_offset;
    uint32_t token_count;
    uint32_t token_table_size;
} ch_save_header;

typedef struct ch_state_file {
    struct ch_state_file* next;
    char id[4];
    const char* name;
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
