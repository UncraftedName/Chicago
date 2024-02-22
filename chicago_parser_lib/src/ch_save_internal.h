#pragma once

#include <stdlib.h>

#include "ch_byte_reader.h"
#include "ch_save.h"

typedef int ch_symbol_offset;

typedef struct ch_symbol_table {
    const char* symbols;
    ch_symbol_offset n_symbols;
    ch_symbol_offset* symbol_offs;
} ch_symbol_table;

// a wrapper of the parsed data with some additional context
typedef struct ch_parsed_save_ctx {
    ch_parsed_save_data* data;
    ch_byte_reader br;
    ch_symbol_table st;
} ch_parsed_save_ctx;

// free the table and set symbols & n_symbols before calling (n_symbols should be positive)
// br should have end set to the end of the symbol table
ch_err ch_br_read_symbol_table(ch_byte_reader* br, ch_symbol_table* st);

inline void ch_free_symbol_table(ch_symbol_table* st)
{
    free(st->symbol_offs);
    st->symbol_offs = NULL;
}

ch_err ch_parse_save_ctx(ch_parsed_save_ctx* ctx);
ch_err ch_parse_state_file(ch_parsed_save_ctx* ctx, ch_state_file* sf);
ch_err ch_parse_hl1(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf);
ch_err ch_parse_hl2(ch_parsed_save_ctx* ctx, ch_sf_adjacent_client_state* sf);
