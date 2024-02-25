#pragma once

#include "SDK/datamap.h"
#include "ch_byte_reader.h"
#include "ch_save_internal.h"

typedef struct ch_block {
    short size_bytes;
    char _pad[2];
    const char* symbol;
} ch_block;

ch_err ch_br_read_symbol(ch_byte_reader* br, const ch_symbol_table* st, const char** symbol);

ch_err ch_br_parse_block(ch_byte_reader* br, const ch_symbol_table* st, ch_block *block);

// make sure to free fields_out even in case of error!
ch_err ch_br_read_save_fields(ch_parsed_save_ctx* ctx,
                              const char* name,
                              const ch_datamap* map,
                              ch_parsed_fields* fields_out);
