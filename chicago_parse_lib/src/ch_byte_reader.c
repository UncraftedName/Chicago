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

ch_err ch_br_read_save_fields(ch_parsed_save_ctx* ctx,
                              const char* name,
                              const ch_datamap* map,
                              ch_parsed_fields* fields_out)
{
    // CRestore::ReadFields

    memset(fields_out, 0, sizeof *fields_out);
    fields_out->map = map;

    ch_byte_reader* br = &ctx->br;

    if (ch_br_read_16(br) != 4)
        return CH_ERR_BAD_FIELDS_MARKER;

    const char* symbol;
    ch_err err = ch_br_read_symbol(br, &ctx->st, &symbol);
    if (err)
        return err;
    // TODO this is a great place to log errors
    if (_stricmp(symbol, name))
        return CH_ERR_BAD_SYMBOL;

    // first calculate how much space we need

    int num_field_buf_bytes = 0;
    int n_fields = ch_br_read_32(br);
    // most of the time fields will be stored in the same order as in the datamap
    int cookie = 0;

    ch_byte_reader br_before_fields = *br;

    if (map->n_fields <= 0)
        return CH_ERR_BAD_FIELD_COUNT;

    // TODO merge this calloc with the one below
    fields_out->n_packed_fields = n_fields;
    fields_out->packed_info = calloc(n_fields, sizeof(ch_parsed_field_info));
    if (!fields_out->packed_info)
        return CH_ERR_OUT_OF_MEMORY;

    for (int i = 0; i < n_fields; i++) {
        ch_block block;
        err = ch_br_parse_block(br, &ctx->st, &block);
        if (err)
            return err;

        // CRestore::FindField
        ch_parsed_field_info* info = &fields_out->packed_info[i];
        info->data_off = num_field_buf_bytes;
        const ch_type_description* field = NULL;
        size_t n_tests = 0;
        for (; n_tests < map->n_fields; n_tests++) {
            info->field_idx = cookie++ % map->n_fields;
            field = &map->fields[info->field_idx];
            if (!_stricmp(field->name, block.symbol))
                break;
        }
        // TODO this is a great place to log errors
        if (n_tests == map->n_fields)
            return CH_ERR_FIELD_NOT_FOUND;

        if (field->type <= FIELD_VOID || field->type >= FIELD_TYPECOUNT)
            return CH_ERR_BAD_FIELD_TYPE;

        if (field->type == FIELD_CUSTOM) {
            // TODO handle custom fields
            info->data_len = -1;
        } else {
            // for simple fields we will just memcpy so we can assume this to be true
            // TODO we might guess what the fields are - so add a check here to see if the block size is a multiple of the field count
            info->data_len = block.size_bytes;
        }
        if (info->data_len > 0)
            num_field_buf_bytes += info->data_len;
        ch_br_skip(br, block.size_bytes);
    }

    if (num_field_buf_bytes == 0)
        return CH_ERR_NONE;

    fields_out->packed_data = calloc(num_field_buf_bytes, 1);
    if (!fields_out->packed_data)
        return CH_ERR_OUT_OF_MEMORY;

    // now read the fields

    for (int i = 0; i < n_fields; i++) {
        ch_parsed_field_info* field_info = &fields_out->packed_info[i];
        const ch_type_description* field = &map->fields[field_info->field_idx];
        short block_size_bytes = ch_br_read_16_unchecked(&br_before_fields);
        ch_br_skip_unchecked(&br_before_fields, 2);
        if (field->type == FIELD_CUSTOM) {
            // TODO handle custom fields
        } else {
            memcpy(&fields_out->packed_data[field_info->data_off], br_before_fields.cur, field_info->data_len);
            // TODO handle adjustments (e.g. time)
        }
        ch_br_skip_unchecked(&br_before_fields, block_size_bytes);
    }

    return CH_ERR_NONE;
}
