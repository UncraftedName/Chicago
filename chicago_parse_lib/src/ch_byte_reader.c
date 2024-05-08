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

ch_err ch_br_start_block(const ch_symbol_table* st, ch_byte_reader* br_cur, ch_block* block)
{
    int16_t size_bytes = ch_br_read_16(br_cur);
    if (size_bytes < 0)
        return CH_ERR_BAD_BLOCK_START;
    CH_RET_IF_ERR(ch_br_read_symbol(br_cur, st, &block->symbol));
    block->reader_after_block = ch_br_split_skip_swap(br_cur, size_bytes);
    if (br_cur->overflowed)
        return CH_ERR_BAD_BLOCK_START;
    return CH_ERR_NONE;
}

ch_err ch_br_end_block(ch_byte_reader* br, ch_block* block)
{
    if (br->cur != block->reader_after_block.cur)
        return CH_ERR_BAD_BLOCK_END;
    *br = block->reader_after_block;
    return CH_ERR_NONE;
}

static ch_err ch_br_restore_simple_field(ch_parsed_save_ctx* ctx, unsigned char* dest, const ch_type_description* td)
{
    ch_field_type ft = td->type;
    ch_byte_reader* br = &ctx->br;

    switch (ft) {
        case FIELD_STRING:
        case FIELD_MODELNAME:
        case FIELD_SOUNDNAME:
        case FIELD_FUNCTION:
        case FIELD_MODELINDEX:
        case FIELD_MATERIALINDEX: {
            for (size_t i = 0; br->cur < br->end && i < td->n_elems; i++) {
                if (*br->cur) {
                    size_t len = strnlen((const char*)br->cur, (size_t)(br->end - br->cur));
                    char** alloc_dest = (char**)dest + i;
                    CH_CHECKED_ALLOC(*alloc_dest, ch_arena_alloc(ctx->arena, len + 1));
                    ch_br_read(br, *alloc_dest, len);
                    (*alloc_dest)[len] = '\0';
                }
                if (br->cur < br->end)
                    ch_br_skip_unchecked(br, 1);
            }
            break;
        }
        case FIELD_FLOAT:
        case FIELD_VECTOR:
        case FIELD_QUATERNION:
        case FIELD_INTEGER:
        case FIELD_BOOLEAN:
        case FIELD_SHORT:
        case FIELD_CHARACTER:
        case FIELD_COLOR32:
        case FIELD_CLASSPTR:
        case FIELD_EHANDLE:
        case FIELD_EDICT:
        case FIELD_POSITION_VECTOR:
        case FIELD_TIME:
        case FIELD_TICK:
        case FIELD_VMATRIX:
        case FIELD_VMATRIX_WORLDSPACE:
        case FIELD_MATRIX3X4_WORLDSPACE:
        case FIELD_INTERVAL:
        case FIELD_VECTOR2D: {
            size_t bytes_avail = (size_t)(br->end - br->cur);
            assert(td->total_size_bytes && bytes_avail >= td->total_size_bytes);
            ch_br_read(br, dest, min(td->total_size_bytes, bytes_avail));
            break;
        }
        case FIELD_INPUT:
        case FIELD_VOID:
        case FIELD_CUSTOM:
        case FIELD_EMBEDDED:
        case FIELD_TYPECOUNT:
        default:
            assert(0);
            return CH_ERR_BAD_FIELD_TYPE;
    }

    // TODO apply field-specific fixups

    return ctx->br.overflowed ? CH_ERR_READER_OVERFLOWED : CH_ERR_NONE;
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
    CH_RET_IF_ERR(ch_br_read_symbol(br, &ctx->st, &symbol));

    if (_stricmp(symbol, expected_symbol)) {
        CH_PARSER_LOG_ERR(ctx, "error attempting to read symbol %s, expected %s", symbol, expected_symbol);
        return CH_ERR_BAD_SYMBOL;
    }

    // most of the time fields will be stored in the same order as in the datamap
    int cookie = -1;

    int n_fields = ch_br_read_32(br);

    for (int i = 0; i < n_fields; i++) {
        ch_block block;
        CH_RET_IF_ERR(ch_br_start_block(&ctx->st, &ctx->br, &block));

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
            CH_PARSER_LOG_ERR(ctx, "failed to find field %s while parsing datamap %s", block.symbol, dm->class_name);
            return CH_ERR_FIELD_NOT_FOUND;
        }

        if (field->type <= FIELD_VOID || field->type >= FIELD_TYPECOUNT)
            return CH_ERR_BAD_FIELD_TYPE;

        // read the field!

        if (field->type == FIELD_CUSTOM) {
            CH_PARSER_LOG_ERR(ctx, "CUSTOM fields are not implemented yet (%s.%s)", dm->class_name, field->name);
            ctx->br.cur = ctx->br.end; // skip
        } else if (field->type == FIELD_EMBEDDED) {
            for (int j = 0; j < field->n_elems; j++)
                CH_RET_IF_ERR(ch_br_restore_recursive(ctx,
                                                      field->embedded_map,
                                                      class_ptr + field->ch_offset + field->total_size_bytes * j));
        } else {
            ch_br_restore_simple_field(ctx, class_ptr + field->ch_offset, field);
        }

        CH_RET_IF_ERR(ch_br_end_block(br, &block));
    }

    return CH_ERR_NONE;
}
