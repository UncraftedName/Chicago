#include "ch_save_internal.h"
#include "custom_restore/ch_utl_vector.h"

typedef struct ch_hl1_sections {
    int32_t symbol_table_size_bytes;
    int32_t n_symbols;
    int32_t data_headers_size_bytes;
    // TODO what the fuck is this?
    int32_t n_bytes_data;
} ch_hl1_sections;

ch_err ch_parse_entity_header(ch_parsed_save_ctx* ctx)
{
    ch_parse_save_log_error(ctx, "hello from entity handler");
    return CH_ERR_NONE;
}

typedef struct ch_block_handler {
    const char* name;
    ch_err (*fn_header_parse)(ch_parsed_save_ctx* ctx);
} ch_block_handler;

static const ch_block_handler ch_block_handlers[] = {
    {.name = "Entities", .fn_header_parse = ch_parse_entity_header},
    {.name = "Physics"},
    {.name = "AI"},
    {.name = "Templates"},
    {.name = "ResponseSystem"},
    {.name = "Commentary"},
    {.name = "Achivement"},
};

ch_err ch_parse_hl1(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf)
{
    // CSaveRestore::LoadSaveData

    ch_byte_reader* br = &ctx->br;

    ch_br_read(br, &sf->tag, sizeof sf->tag);
    const ch_tag expected_tag = {.id = {'V', 'A', 'L', 'V'}, .version = 0x73};
    if (memcmp(&sf->tag, &expected_tag, sizeof expected_tag))
        return CC_ERR_HL1_BAD_TAG;

    ch_hl1_sections sections;
    if (!ch_br_read(br, &sections, sizeof sections))
        return CH_ERR_READER_OVERFLOWED;

    if (sections.symbol_table_size_bytes > 0) {
        ch_byte_reader br_st = ch_br_split_skip(br, sections.symbol_table_size_bytes);
        if (br->overflowed)
            return CH_ERR_READER_OVERFLOWED;
        ch_err err = ch_br_read_symbol_table(&br_st, &ctx->st, sections.n_symbols);
        if (err)
            return err;
    }

    // CSaveRestoreBlockSet::ReadRestoreHeaders

    ch_byte_reader br_base = *br;

    int32_t headers_size_bytes = ch_br_read_32(br);
    int32_t bodies_size_bytes = ch_br_read_32(br);
    if (br->overflowed)
        return CH_ERR_READER_OVERFLOWED;

    ch_cr_utl_vector_restored block_headers;
    ch_err err = ch_cr_utl_vector_restore_by_name_to(ctx, "SaveRestoreBlockHeader_t", &block_headers);
    if (err)
        return err;

    const ch_type_description *td_name, *td_loc_header;
    err = ch_find_field_log_if_dne(ctx, block_headers.embedded_map, "szName", true, &td_name);
    if (err)
        return err;
    err = ch_find_field_log_if_dne(ctx, block_headers.embedded_map, "locHeader", true, &td_loc_header);
    if (err)
        return err;

    /*
    * Here, the game has its own list of handlers and tries to find the corresponding header in save
    * for each handler. We'll do the opposite - for each header that we find in the save, check if we
    * have a corresponding handler. The modulo arithmetic indexing is used here because I expect the
    * handler indices to match exactly with the header indices.
    */

    for (size_t i = 0; i < block_headers.n_elems; i++) {
        unsigned char* header_data = CH_UTL_VEC_ELEM_PTR(block_headers, i);
        const char* header_name = CH_FIELD_AT_PTR(header_data, td_name, const char);
        const ch_block_handler* found_handler = NULL;
        for (size_t j = 0; j < CH_ARRAYSIZE(ch_block_handlers) && !found_handler; j++) {
            const ch_block_handler* test_handler = &ch_block_handlers[(i + j) % CH_ARRAYSIZE(ch_block_handlers)];
            if (!strcmp(test_handler->name, header_name))
                found_handler = test_handler;
        }
        if (!found_handler) {
            ch_parse_save_log_error(ctx, "no registered handler for block header '%s'", header_name);
            continue;
        }
        if (!found_handler->fn_header_parse) {
            ch_parse_save_log_error(ctx, "no associated header parsing handler for block header '%s'", header_name);
            continue;
        }

        *br = ch_br_jmp_rel(&br_base, CH_FIELD_AT(header_data, td_loc_header, int));
        if (br->overflowed) {
            ch_parse_save_log_error(ctx, "bogus header location for block header '%s'", header_name);
            continue;
        }
        ch_err header_err = found_handler->fn_header_parse(ctx);
        if (header_err) {
            ch_parse_save_log_error(ctx, "error parsing block handler '%s': %s", header_name, ch_err_strs[header_err]);
            continue;
        }
    }

    *br = ch_br_jmp_rel(&br_base, headers_size_bytes);
    if (br->overflowed)
        return CH_ERR_READER_OVERFLOWED;

    (void)bodies_size_bytes;

    return CH_ERR_NONE;
}
