#include <string.h>

#include "ch_field_reader.h"
#include "ch_save_internal.h"

ch_err ch_br_read_symbol_table(ch_byte_reader* br, ch_symbol_table* st, int n_symbols)
{
    if (n_symbols < 0)
        return CH_ERR_BAD_SYMBOL_TABLE;
    st->symbols = (const char*)br->cur;
    st->n_symbols = n_symbols;
    // TODO - change to realloc
    st->symbol_offs = calloc(st->n_symbols, sizeof *st->symbol_offs);
    if (!st->symbol_offs)
        return CH_ERR_OUT_OF_MEMORY;

    const unsigned char* start = br->cur;
    for (int i = 0; i < n_symbols && !br->overflowed; i++) {
        if (*br->cur)
            st->symbol_offs[i] = br->cur - start;
        ch_br_skip(br, strnlen((const char*)br->cur, br->end - br->cur) + 1);
    }
    if (br->overflowed) {
        ch_free_symbol_table(st);
        return CH_ERR_BAD_SYMBOL_TABLE; // being more specific than reader overflow
    } else {
        return CH_ERR_NONE;
    }
}

ch_err ch_br_read_symbol(ch_byte_reader* br, const ch_symbol_table* st, const char** symbol)
{
    short idx = ch_br_read_16(br);
    if (br->overflowed)
        return CH_ERR_READER_OVERFLOWED;
    if (idx < 0 || idx >= st->n_symbols)
        return CH_ERR_BAD_SYMBOL;
    *symbol = st->symbols + st->symbol_offs[idx];
    if (!**symbol)
        return CH_ERR_BAD_SYMBOL;
    return CH_ERR_NONE;
}

ch_err ch_br_parse_block(ch_byte_reader* br, const ch_symbol_table* st, ch_block* block)
{
    block->size_bytes = ch_br_read_16(br);
    if (block->size_bytes < 0)
        return CH_ERR_BAD_BLOCK_SIZE;
    return ch_br_read_symbol(br, st, &block->symbol);
}

ch_err ch_br_restore_fields(ch_parsed_save_ctx* ctx,
                            const char* expected_symbol,
                            const ch_datamap* dm,
                            unsigned char* class_ptr)
{
    // CRestore::ReadFields

    ch_byte_reader* br = &ctx->br;

    if (ch_br_read_16(br) != 4)
        return CH_ERR_BAD_FIELDS_MARKER;

    const char* symbol;
    ch_err err = ch_br_read_symbol(br, &ctx->st, &symbol);
    if (err)
        return err;

    if (_stricmp(symbol, expected_symbol)) {
        ch_parse_save_log_error(ctx,
                                "%s: Error attempting to read symbol %s, expected %s",
                                __FUNCTION__,
                                symbol,
                                expected_symbol);
        return CH_ERR_BAD_SYMBOL;
    }

    // most of the time fields will be stored in the same order as in the datamap
    int cookie = 0;

    int n_fields = ch_br_read_32(br);

    for (int i = 0; i < n_fields; i++) {
        ch_block block;
        err = ch_br_parse_block(br, &ctx->st, &block);
        if (err)
            return err;

        // CRestore::FindField
        const ch_type_description* field = NULL;
        size_t n_tests = 0;
        for (; n_tests < dm->n_fields; n_tests++) {
            cookie = (cookie + 1) % dm->n_fields;
            field = &dm->fields[cookie];
            if (!_stricmp(field->name, block.symbol))
                break;
        }

        if (n_tests == dm->n_fields) {
            ch_parse_save_log_error(ctx,
                                    "%s: Failed to find field %s while parsing datamap %s",
                                    __FUNCTION__,
                                    block.symbol,
                                    dm->class_name);
            return CH_ERR_FIELD_NOT_FOUND;
        }

        if (field->type <= FIELD_VOID || field->type >= FIELD_TYPECOUNT)
            return CH_ERR_BAD_FIELD_TYPE;

        // read the field!

        if (field->type == FIELD_CUSTOM) {
            ch_parse_save_log_error(ctx,
                                    "%s: CUSTOM fields are not implemented yet (%s.%s)",
                                    __FUNCTION__,
                                    dm->class_name,
                                    field->name);
            ch_br_skip(br, block.size_bytes);
        } else if (field->type == FIELD_EMBEDDED) {
            for (int j = 0; j < field->n_elems; j++) {
                err = ch_br_restore_recursive(ctx,
                                              field->embedded_map,
                                              class_ptr + field->ch_offset + field->total_size_bytes * j);
                if (!err)
                    return err;
            }
        } else {
            assert(block.size_bytes != 0);
            ch_br_read(br, class_ptr + field->ch_offset, block.size_bytes);
            // TODO apply field-specific fixups
        }
    }

    return CH_ERR_NONE;
}
