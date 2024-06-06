#include "ch_save_internal.h"
#include "ch_field_reader.h"

ch_err ch_parse_block_templates_header(ch_parsed_save_ctx* ctx, ch_block_templates* block)
{
    block->version = ch_br_read_16(&ctx->br);
    CH_RET_IF_BR_OVERFLOWED(&ctx->br);
    return CH_ERR_NONE;
}

ch_err ch_parse_block_templates_body(ch_parsed_save_ctx* ctx, ch_block_templates* block)
{
    // CTemplate_SaveRestoreBlockHandler::Restore

    if (block->version != CH_HEADER_TEMPLATE_SAVE_RESTORE_VERSION) {
        CH_PARSER_LOG_ERR(ctx, "funny templates version: %d", block->version);
        return CH_ERR_UNSUPPORTED_BLOCK_VERSION;
    }

    ch_byte_reader* br = &ctx->br;

    const ch_type_description* td_map_data_len;
    CH_RET_IF_ERR(ch_lookup_datamap(ctx, "TemplateEntityData_t", &block->dm_template));
    CH_RET_IF_ERR(
        ch_find_field_log_if_dne(ctx, block->dm_template, "iMapDataLength", true, &td_map_data_len, FIELD_INTEGER));

    block->template_instance = ch_br_read_32(br);
    block->n_templates = ch_br_read_16(br);
    CH_CHECKED_ALLOC(block->templates, ch_arena_calloc(ctx->arena, sizeof(ch_template_data) * block->n_templates));

    for (int16_t i = 0; i < block->n_templates; i++) {
        ch_template_data* tpl = &block->templates[i];
        CH_CHECKED_ALLOC(tpl->template_data, ch_arena_calloc(ctx->arena, block->dm_template->ch_size));
        CH_RET_IF_ERR(ch_br_restore_recursive(ctx, block->dm_template, tpl->template_data));
        CH_RET_IF_ERR(ch_br_read_str(br, ctx->arena, &tpl->name));
        CH_RET_IF_ERR(ch_br_read_str(br, ctx->arena, &tpl->map_data));
    }
    return CH_ERR_NONE;
}
