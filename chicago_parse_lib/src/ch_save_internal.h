#pragma once

#include <stdlib.h>
#include <stdarg.h>

#include "ch_byte_reader.h"
#include "ch_save.h"

#include "block_handlers/ch_block_ents.h"

#define CH_ARRAYSIZE(a) (sizeof(a) / sizeof(*(a)))

#define CH_RET_IF_ERR(x)     \
    do {                     \
        ch_err _tmp_err = x; \
        if (_tmp_err)        \
            return _tmp_err; \
    } while (0)

typedef struct ch_symbol_table {
    const char* symbols;
    int n_symbols;
    int* symbol_offs;
} ch_symbol_table;

// a wrapper of the parsed data with some additional context
typedef struct ch_parsed_save_ctx {
    const ch_parse_info* info;
    ch_parsed_save_data* data;
    ch_arena* arena; // same pointer as in the save data
    ch_byte_reader br;
    ch_symbol_table st;
    ch_parse_save_error* last_error;

    // blocks
    ch_block_ents block_ents;
} ch_parsed_save_ctx;

ch_err ch_parse_save_log_error(ch_parsed_save_ctx* ctx, const char* fmt, ...);

// Creates and allocates symbol_offs if CH_ERR_NONE - make sure to free it!
// br should be only big enough to fit the symbol table.
ch_err ch_br_read_symbol_table(ch_byte_reader* br, ch_symbol_table* st, int n_symbols);

static inline void ch_free_symbol_table(ch_symbol_table* st)
{
    free(st->symbol_offs);
    memset(st, 0, sizeof *st);
}

ch_err ch_parse_save_ctx(ch_parsed_save_ctx* ctx);
ch_err ch_parse_state_file(ch_parsed_save_ctx* ctx, ch_state_file* sf);
ch_err ch_parse_hl1(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf);
ch_err ch_parse_hl2(ch_parsed_save_ctx* ctx, ch_sf_adjacent_client_state* sf);
ch_err ch_parse_hl3(ch_parsed_save_ctx* ctx, ch_sf_entity_patch* sf);

static inline ch_err ch_lookup_datamap(ch_parsed_save_ctx* ctx, const char* name, const ch_datamap** dm)
{
    assert(name);
    ch_datamap_lookup_entry entry_in = {.name = name};
    const ch_datamap_lookup_entry* entry_out = hashmap_get(ctx->info->datamap_collection->lookup, &entry_in);
    if (!entry_out) {
        ch_parse_save_log_error(ctx, "datamap '%s' not found in collection", name);
        return CH_ERR_DATAMAP_NOT_FOUND;
    }
    *dm = entry_out->datamap;
    return CH_ERR_NONE;
}

// TODO check field type
static inline ch_err ch_find_field_log_if_dne(ch_parsed_save_ctx* ctx,
                                              const ch_datamap* dm,
                                              const char* field_name,
                                              bool recurse_base_classes,
                                              const ch_type_description** field)
{
    ch_err err = ch_find_field(dm, field_name, recurse_base_classes, field);
    if (err)
        ch_parse_save_log_error(ctx, "failed to find filed '%s' in '%s'", field_name, dm->class_name);
    return err;
}
