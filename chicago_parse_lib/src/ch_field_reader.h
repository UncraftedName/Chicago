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

ch_err ch_br_parse_block(ch_byte_reader* br, const ch_symbol_table* st, ch_block* block);

// CRestore::ReadFeilds
ch_err ch_br_restore_fields(ch_parsed_save_ctx* ctx,
                            const char* symbol_name,
                            const ch_datamap* map,
                            unsigned char* class_ptr);

// CRestore::DoReadAll
static ch_err ch_br_restore_recursive(ch_parsed_save_ctx* ctx, const ch_datamap* dm, unsigned char* class_ptr)
{
    if (dm->base_map) {
        ch_err err = ch_br_restore_recursive(ctx, dm->base_map, class_ptr);
        if (err != CH_ERR_NONE)
            return err;
    }
    return ch_br_restore_fields(ctx, dm->class_name, dm, class_ptr);
}

// todo this handles the normal case & the recursive one, maybe they should be separate functions
static ch_err ch_br_restore_class_by_name(ch_parsed_save_ctx* ctx,
                                          const char* symbol_name,
                                          const char* class_name,
                                          ch_restored_class* restored_class)
{
    ch_err ret = ch_lookup_datamap(ctx, class_name, &restored_class->dm);
    if (ret != CH_ERR_NONE)
        return ret;
    restored_class->data = ch_arena_calloc(ctx->arena, restored_class->dm->ch_size);
    if (!restored_class->data)
        return CH_ERR_OUT_OF_MEMORY;
    assert(!!symbol_name ^ !!restored_class->dm->base_map);
    if (symbol_name)
        return ch_br_restore_fields(ctx, symbol_name, restored_class->dm, restored_class->data);
    else
        return ch_br_restore_recursive(ctx, restored_class->dm, restored_class->data);
}
