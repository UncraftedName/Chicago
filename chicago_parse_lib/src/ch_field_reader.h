#pragma once

#include "SDK/datamap.h"
#include "ch_byte_reader.h"
#include "ch_save_internal.h"

typedef struct ch_record {
    ch_byte_reader reader_after_block;
    const char* symbol;
} ch_record;

ch_err ch_br_read_symbol(ch_byte_reader* br, const ch_symbol_table* st, const char** symbol);

ch_err ch_br_start_record(const ch_symbol_table* st, ch_byte_reader* br_cur, ch_record* block);
ch_err ch_br_end_record(ch_byte_reader* br, ch_record* block, bool check_match);

ch_err ch_br_read_str(ch_byte_reader* br, ch_arena* arena, char** str_out);
ch_err ch_br_read_str_n(ch_byte_reader* br, ch_arena* arena, char** str_out, size_t read_bytes);

ch_err ch_br_restore_simple_field(ch_parsed_save_ctx* ctx,
                                  void* dest,
                                  ch_field_type ft,
                                  size_t n_elems,
                                  size_t total_size_bytes);

// CRestore::ReadFields
ch_err ch_br_restore_fields(ch_parsed_save_ctx* ctx,
                            const char* symbol_name,
                            const ch_datamap* map,
                            unsigned char* class_ptr);

// CRestore::DoReadAll
static ch_err ch_br_restore_recursive(ch_parsed_save_ctx* ctx, const ch_datamap* dm, unsigned char* class_ptr)
{
    if (dm->base_map)
        CH_RET_IF_ERR(ch_br_restore_recursive(ctx, dm->base_map, class_ptr));
    return ch_br_restore_fields(ctx, dm->class_name, dm, class_ptr);
}

static ch_err ch_br_restore_class_by_name(ch_parsed_save_ctx* ctx,
                                          const char* override_symbol,
                                          const char* class_name,
                                          ch_restored_class* restored_class)
{
    CH_RET_IF_ERR(ch_lookup_datamap(ctx, class_name, &restored_class->dm));
    CH_CHECKED_ALLOC(restored_class->data, ch_arena_calloc(ctx->arena, restored_class->dm->ch_size));
    assert(!!override_symbol || !restored_class->dm->base_map);
    if (override_symbol)
        return ch_br_restore_fields(ctx, override_symbol, restored_class->dm, restored_class->data);
    else
        return ch_br_restore_recursive(ctx, restored_class->dm, restored_class->data);
}
