#pragma once

#include <stdlib.h>

#include "ch_byte_reader.h"
#include "ch_save.h"

typedef struct ch_symbol_table {
    const char* symbols;
    int n_symbols;
    int* symbol_offs;
} ch_symbol_table;

// a wrapper of the parsed data with some additional context
typedef struct ch_parsed_save_ctx {
    const ch_parse_info* info;
    ch_parsed_save_data* data;
    ch_byte_reader br;
    ch_symbol_table st;
} ch_parsed_save_ctx;

// Creates and allocates symbol_offs if CH_ERR_NONE - make sure to free it!
// br should be only big enough to fit the symbol table.
ch_err ch_br_read_symbol_table(ch_byte_reader* br, ch_symbol_table* st, int n_symbols);

inline void ch_free_symbol_table(ch_symbol_table* st)
{
    free(st->symbol_offs);
    memset(st, 0, sizeof *st);
}

ch_err ch_parse_save_ctx(ch_parsed_save_ctx* ctx);
ch_err ch_parse_state_file(ch_parsed_save_ctx* ctx, ch_state_file* sf);
ch_err ch_parse_hl1(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf);
ch_err ch_parse_hl2(ch_parsed_save_ctx* ctx, ch_sf_adjacent_client_state* sf);
ch_err ch_parse_hl3(ch_parsed_save_ctx* ctx, ch_sf_entity_patch* sf);
