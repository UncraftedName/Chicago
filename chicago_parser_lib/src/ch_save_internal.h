#pragma once

#include "ch_byte_reader.h"
#include "ch_save.h"

typedef int ch_symbol_offset;

// a wrapper of the parsed data with some additional context
typedef struct ch_parsed_save_ctx {

    ch_parsed_save_data* data;

    ch_byte_reader br;

    // symbol table
    const char* symbols;
    ch_symbol_offset n_symbols;
    ch_symbol_offset* symbol_offs;

} ch_parsed_save_ctx;

ch_err ch_parse_save_ctx(ch_parsed_save_ctx* ctx);
ch_err ch_parse_state_file(ch_parsed_save_ctx* ctx, ch_state_file* sf);
ch_err ch_parse_sf_save_data(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf);
