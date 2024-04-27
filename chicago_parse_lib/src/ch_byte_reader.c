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
            const char* p = (const char*)br->cur;
            for (size_t i = 0; p < (const char*)br->end && i < td->n_elems; i++) {
                if (*p) {
                    size_t len = strnlen(p, (size_t)((const char*)br->end - p));
                    char** alloc_dest = (char**)dest + i;
                    *alloc_dest = ch_arena_alloc(ctx->arena, len + 1);
                    if (!*alloc_dest)
                        return CH_ERR_OUT_OF_MEMORY;
                    strncpy(*alloc_dest, p, len + 1);
                    p += len + 1;
                } else {
                    p++;
                }
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
            assert(td->total_size_bytes);
            ch_br_read(br, dest, min(td->total_size_bytes, (size_t)(br->end - br->cur)));
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
        ch_parse_save_log_error(ctx,
                                "%s: Error attempting to read symbol %s, expected %s",
                                __FUNCTION__,
                                symbol,
                                expected_symbol);
        return CH_ERR_BAD_SYMBOL;
    }

    // most of the time fields will be stored in the same order as in the datamap
    int cookie = -1;

    int n_fields = ch_br_read_32(br);

    for (int i = 0; i < n_fields; i++) {
        ch_block block;
        CH_RET_IF_ERR(ch_br_parse_block(br, &ctx->st, &block));

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

        ch_byte_reader br_after_field = ch_br_split_skip_swap(br, block.size_bytes);

        if (field->type == FIELD_CUSTOM) {
            ch_parse_save_log_error(ctx,
                                    "%s: CUSTOM fields are not implemented yet (%s.%s)",
                                    __FUNCTION__,
                                    dm->class_name,
                                    field->name);
        } else if (field->type == FIELD_EMBEDDED) {
            for (int j = 0; j < field->n_elems; j++)
                CH_RET_IF_ERR(ch_br_restore_recursive(ctx,
                                                      field->embedded_map,
                                                      class_ptr + field->ch_offset + field->total_size_bytes * j));
        } else {
            ch_br_restore_simple_field(ctx, class_ptr + field->ch_offset, field);
        }

        *br = br_after_field;
    }

    return CH_ERR_NONE;
}
