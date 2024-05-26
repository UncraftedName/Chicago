#include "ch_save_internal.h"
#include "ch_field_reader.h"
#include "custom_restore/ch_utl_vector.h"
#include "block_handlers/ch_block_ents.h"

typedef struct ch_hl1_sections {
    int32_t symbol_table_size_bytes;
    int32_t n_symbols;
    int32_t data_headers_size_bytes;
    // TODO what the fuck is this?
    int32_t n_bytes_data;
} ch_hl1_sections;

typedef struct ch_block_handler {
    const char* name;
    size_t alloc_size;
    ch_err (*fn_parse_header)(ch_parsed_save_ctx* ctx, void* block_data);
    ch_err (*fn_parse_body)(ch_parsed_save_ctx* ctx, void* block_data);
} ch_block_handler;

/*
* Each block has an associated header & body. If information needs to be
* shared between them, then it should be stored in either the ctx or the
* parsed save data structure. If the header function failed or doesn't
* exist, the corresponding body function won't be called. These MUST be
* in the same order as the ch_block_type enum.
*/
static const ch_block_handler ch_block_handlers[CH_BLOCK_COUNT] = {
    {
        .name = "Entities",
        .alloc_size = sizeof(ch_block_entities),
        .fn_parse_header = ch_parse_entity_block_header,
        .fn_parse_body = ch_parse_entity_block_body,
    },
    {
        .name = "Physics",
        .alloc_size = sizeof(ch_block_physics),
    },
    {
        .name = "AI",
        .alloc_size = sizeof(ch_block_ai),
    },
    {
        .name = "Templates",
        .alloc_size = sizeof(ch_block_templates),
    },
    {
        .name = "ResponseSystem",
        .alloc_size = sizeof(ch_block_response_system),
    },
    {
        .name = "Commentary",
        .alloc_size = sizeof(ch_block_commentary),
    },
    {
        .name = "EventQueue",
        .alloc_size = sizeof(ch_block_event_queue),
    },
    {
        .name = "Achievement",
        .alloc_size = sizeof(ch_block_achievements),
    },
};

static ch_err ch_restore_block_headers(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf, int32_t headers_size_bytes)
{
    // CSaveRestoreBlockSet::ReadRestoreHeaders

    CH_CHECKED_ALLOC(sf->block_headers, ch_arena_calloc(ctx->arena, sizeof *sf->block_headers));
    CH_RET_IF_ERR(ch_cr_utl_vector_restore_by_name_to(ctx, "SaveRestoreBlockHeader_t", sf->block_headers));

    const ch_type_description *td_name, *td_loc_header;
    CH_RET_IF_ERR(
        ch_find_field_log_if_dne(ctx, sf->block_headers->embedded_map, "szName", true, &td_name, FIELD_CHARACTER));
    CH_RET_IF_ERR(ch_find_field_log_if_dne(ctx,
                                           sf->block_headers->embedded_map,
                                           "locHeader",
                                           true,
                                           &td_loc_header,
                                           FIELD_INTEGER));

    /*
    * Here, the game has its own list of handlers and tries to find the corresponding header in the file
    * for each handler. We'll do the opposite - for each header that we find in the save, check if we
    * have a corresponding handler.
    */

    for (size_t i = 0; i < sf->block_headers->n_elems; i++) {
        unsigned char* header_data = CH_UTL_VEC_ELEM_PTR(*sf->block_headers, i);
        const char* header_name = CH_FIELD_AT_PTR(header_data, td_name, const char);
        size_t j = 0;
        for (; j < CH_BLOCK_COUNT; j++)
            if (!strcmp(ch_block_handlers[j].name, header_name))
                break;
        if (j == CH_BLOCK_COUNT) {
            CH_PARSER_LOG_ERR(ctx, "no registered handlers for block with name '%s'", header_name);
            continue;
        }
        const ch_block_handler* handler = &ch_block_handlers[j];
        ch_block* block = &sf->blocks[j];

        int32_t header_loc = CH_FIELD_AT(header_data, td_loc_header, int32_t);
        if (header_loc == -1)
            continue;
        if (block->header_parsed) {
            CH_PARSER_LOG_ERR(ctx, "more than one header section for block '%s'", header_name);
            continue;
        }
        block->vec_idx = (int16_t)(i + 1);
        if (!handler->fn_parse_header) {
            CH_PARSER_LOG_ERR(ctx, "no associated handler for parsing header of block '%s'", header_name);
            continue;
        }
        ctx->br = ch_br_jmp_rel(&ctx->br_cur_base, header_loc);
        if (ctx->br.overflowed) {
            CH_PARSER_LOG_ERR(ctx, "bogus header location for block '%s'", header_name);
            continue;
        }
        CH_CHECKED_ALLOC(block->data, ch_arena_calloc(ctx->arena, handler->alloc_size));
        ch_err header_err = handler->fn_parse_header(ctx, block->data);
        if (header_err == CH_ERR_OUT_OF_MEMORY) {
            return header_err;
        } else if (header_err) {
            CH_PARSER_LOG_ERR(ctx, "error parsing header for block '%s': %s", header_name, ch_err_strs[header_err]);
            continue;
        }
        block->header_parsed = true;
    }

    ctx->br = ch_br_jmp_rel(&ctx->br_cur_base, headers_size_bytes);
    if (ctx->br.overflowed)
        return CH_ERR_READER_OVERFLOWED;

    return CH_ERR_NONE;
}

static ch_err ch_restore_save_tables(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf)
{
    // CSaveRestore::ParseSaveTables

    /*
    * Entities are restored relative to this base. There is *another* base in restore_block_bodies which is only a
    * *local* var. The entity body restore explicitly jumps relative to the base stored in the IRestore object.
    */
    ctx->br_cur_base = ctx->br;

    CH_RET_IF_ERR(ch_br_restore_class_by_name(ctx, "Save Header", "SAVE_HEADER", &sf->save_header));
    // TODO setup landmark info here from the header

    const ch_type_description *td_n_connections, *td_n_light_styles;
    CH_RET_IF_ERR(
        ch_find_field_log_if_dne(ctx, sf->save_header.dm, "connectionCount", true, &td_n_connections, FIELD_INTEGER));
    CH_RET_IF_ERR(
        ch_find_field_log_if_dne(ctx, sf->save_header.dm, "lightStyleCount", true, &td_n_light_styles, FIELD_INTEGER));

    sf->adjacent_levels.n_elems = CH_FIELD_AT(sf->save_header.data, td_n_connections, int32_t);
    sf->light_styles.n_elems = CH_FIELD_AT(sf->save_header.data, td_n_light_styles, int32_t);

    CH_RET_IF_ERR(ch_lookup_datamap(ctx, "levellist_t", &sf->adjacent_levels.dm));
    CH_RET_IF_ERR(ch_lookup_datamap(ctx, "SAVELIGHTSTYLE", &sf->light_styles.dm));

    CH_CHECKED_ALLOC(sf->adjacent_levels.data, ch_arena_calloc(ctx->arena, CH_RCA_DATA_SIZE(sf->adjacent_levels)));
    CH_CHECKED_ALLOC(sf->light_styles.data, ch_arena_calloc(ctx->arena, CH_RCA_DATA_SIZE(sf->light_styles)));

    for (size_t i = 0; i < sf->adjacent_levels.n_elems; i++)
        CH_RET_IF_ERR(
            ch_br_restore_fields(ctx, "ADJACENCY", sf->adjacent_levels.dm, CH_RCA_ELEM_DATA(sf->adjacent_levels, i)));

    for (size_t i = 0; i < sf->light_styles.n_elems; i++)
        CH_RET_IF_ERR(
            ch_br_restore_fields(ctx, "LIGHTSTYLE", sf->light_styles.dm, CH_RCA_ELEM_DATA(sf->light_styles, i)));

    return CH_ERR_NONE;
}

static ch_err ch_restore_block_bodies(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf, int32_t bodies_size_bytes)
{
    // CSaveRestoreBlockSet::Restore

    /*
    * This restore function saves the base *locally* - it does not rebase the IRestore object. Most of the bodies simply
    * read the data at this location. If a jump is made inside the body restore however, it does not jump relative to
    * this base - it jumps relative to the base in the IRestore object which we set earlier.
    */
    ch_byte_reader br_local_base = ch_br_split_skip(&ctx->br, bodies_size_bytes);
    ch_byte_reader br_after_bodies = ctx->br;

    const ch_type_description* td_loc_body;
    CH_RET_IF_ERR(
        ch_find_field_log_if_dne(ctx, sf->block_headers->embedded_map, "locBody", true, &td_loc_body, FIELD_INTEGER));

    for (size_t i = 0; i < CH_BLOCK_COUNT; i++) {
        ch_block* block = &sf->blocks[i];
        const ch_block_handler* handler = &ch_block_handlers[i];
        if (!block->header_parsed || block->vec_idx == 0)
            continue;
        if (!handler->fn_parse_body) {
            CH_PARSER_LOG_ERR(ctx, "no associated handler for parsing body of block '%s'", handler->name);
            continue;
        }

        int32_t body_loc = CH_FIELD_AT(CH_UTL_VEC_ELEM_PTR(*sf->block_headers, block->vec_idx), td_loc_body, int32_t);

        if (body_loc == -1)
            continue;
        ctx->br = ch_br_jmp_rel(&br_local_base, body_loc);
        if (ctx->br.overflowed) {
            CH_PARSER_LOG_ERR(ctx, "bogus body location for block '%s'", handler->name);
            continue;
        }
        ch_err err = handler->fn_parse_body(ctx, block->data);
        if (err == CH_ERR_OUT_OF_MEMORY) {
            return err;
        } else if (err) {
            CH_PARSER_LOG_ERR(ctx, "error parsing header for block '%s': %s", handler->name, ch_err_strs[err]);
            continue;
        }
        block->body_parsed = true;
    }
    if (br_after_bodies.overflowed)
        return CH_ERR_READER_OVERFLOWED;
    ctx->br = br_after_bodies;
    return CH_ERR_NONE;
}

ch_err ch_parse_hl1(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf)
{
    // CSaveRestore::LoadGameState

    ch_byte_reader* br = &ctx->br;

    ch_br_read(br, &sf->tag, sizeof sf->tag);
    const ch_tag expected_tag = {.id = {'V', 'A', 'L', 'V'}, .version = 0x73};
    if (memcmp(&sf->tag, &expected_tag, sizeof expected_tag))
        return CH_ERR_HL1_BAD_TAG;

    ch_hl1_sections sections;
    if (!ch_br_read(br, &sections, sizeof sections))
        return CH_ERR_READER_OVERFLOWED;

    if (sections.symbol_table_size_bytes > 0) {
        ch_byte_reader br_st = ch_br_split_skip(br, sections.symbol_table_size_bytes);
        if (br->overflowed)
            return CH_ERR_READER_OVERFLOWED;
        CH_RET_IF_ERR(ch_br_read_symbol_table(&br_st, &ctx->st, sections.n_symbols));
    }

    ctx->br_cur_base = ctx->br;
    int32_t headers_size_bytes = ch_br_read_32(br);
    int32_t bodies_size_bytes = ch_br_read_32(br);
    if (br->overflowed)
        return CH_ERR_READER_OVERFLOWED;

    CH_RET_IF_ERR(ch_restore_block_headers(ctx, sf, headers_size_bytes));
    CH_RET_IF_ERR(ch_restore_save_tables(ctx, sf));
    // game skips to the .hl3 file here, we have no reason to do that
    CH_RET_IF_ERR(ch_restore_block_bodies(ctx, sf, bodies_size_bytes));

    return CH_ERR_NONE;
}
