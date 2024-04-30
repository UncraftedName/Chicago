#include "ch_save_internal.h"
#include "ch_field_reader.h"
#include "custom_restore/ch_utl_vector.h"
#include "ch_block_ents.h"

ch_err ch_restore_entity(ch_parsed_save_ctx* ctx, const char* classname, ch_restored_entity* ent)
{
    // CEntitySaveRestoreBlockHandler::RestoreEntity

    CH_RET_IF_ERR(ch_lookup_datamap(ctx, classname, &ent->class_info.dm));

    printf("restoring %s\n", classname);
    return CH_ERR_NONE;
}

ch_err ch_parse_entity_block_header(ch_parsed_save_ctx* ctx)
{
    // CEntitySaveRestoreBlockHandler::ReadRestoreHeaders

    ch_restored_class_arr* ent_table = &ctx->sf_save_data->blocks.entities.entity_table;

    ent_table->n_elems = ch_br_read_32(&ctx->br);
    if (ctx->br.overflowed)
        return CH_ERR_READER_OVERFLOWED;

    CH_RET_IF_ERR(ch_lookup_datamap(ctx, "entitytable_t", &ent_table->dm));

    ent_table->data = ch_arena_calloc(ctx->arena, CH_RCA_DATA_SIZE(*ent_table));
    if (!ent_table->data)
        return CH_ERR_OUT_OF_MEMORY;

    // initialize some int fields w/ -1
    // the game also inits restoreentityindex but that's not part of the datamap TODO could try to figure out the restored index
    const char* init_fields[] = {"id", "edictindex", "saveentityindex"};
    for (size_t i = 0; i < CH_ARRAYSIZE(init_fields); i++) {
        const ch_type_description* td;
        ch_find_field_log_if_dne(ctx, ent_table->dm, init_fields[i], true, &td, FIELD_INTEGER);
        for (size_t j = 0; td && j < ent_table->n_elems; j++)
            CH_FIELD_AT(CH_RCA_ELEM_DATA(*ent_table, j), td, int32_t) = -1;
    }

    for (size_t i = 0; i < ent_table->n_elems; i++)
        CH_RET_IF_ERR(ch_br_restore_fields(ctx, "ETABLE", ent_table->dm, CH_RCA_ELEM_DATA(*ent_table, i)));

    return CH_ERR_NONE;
}

ch_err ch_parse_entity_block_body(ch_parsed_save_ctx* ctx)
{
    // CEntitySaveRestoreBlockHandler::Restore

    ch_block_entities* block = &ctx->sf_save_data->blocks.entities;
    const ch_datamap* dm_ent_table = block->entity_table.dm;

    block->entities = ch_arena_calloc(ctx->arena, sizeof(ch_restored_entity) * block->entity_table.n_elems);
    if (!block->entities)
        return CH_ERR_OUT_OF_MEMORY;

    ch_type_description *td_classname, *td_size, *td_loc;
    CH_RET_IF_ERR(ch_find_field_log_if_dne(ctx, dm_ent_table, "classname", true, &td_classname, FIELD_STRING));
    CH_RET_IF_ERR(ch_find_field_log_if_dne(ctx, dm_ent_table, "size", true, &td_size, FIELD_INTEGER));
    CH_RET_IF_ERR(ch_find_field_log_if_dne(ctx, dm_ent_table, "location", true, &td_loc, FIELD_INTEGER));

    for (size_t i = 0; i < block->entity_table.n_elems; i++) {
        const unsigned char* ent_table_info = CH_RCA_ELEM_DATA(block->entity_table, i);
        const char* classname = CH_FIELD_AT(ent_table_info, td_classname, const char*);
        int32_t size = CH_FIELD_AT(ent_table_info, td_size, int32_t);
        int32_t loc = CH_FIELD_AT(ent_table_info, td_loc, int32_t);

        if (!classname || !size)
            continue;

        ctx->br = ch_br_jmp_rel(&ctx->br_cur_base, loc);
        if (ctx->br.overflowed) {
            CH_PARSER_LOG_ERR(ctx, "bogus restore location for entity '%s' at index %d", classname, (int)i);
            continue;
        }
        ch_err err = ch_restore_entity(ctx, classname, &block->entities[i]);
        if (err == CH_ERR_OUT_OF_MEMORY)
            return err;
        else if (err != CH_ERR_DATAMAP_NOT_FOUND)
            CH_PARSER_LOG_ERR(ctx, "'ch_restore_entity' failed: %s", ch_err_strs[err]);
    }
    return CH_ERR_NONE;
}
