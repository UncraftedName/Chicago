#include "ch_save_internal.h"
#include "ch_field_reader.h"
#include "custom_restore/ch_utl_vector.h"
#include "ch_block_ents.h"

static ch_err ch_restore_conditions(ch_parsed_save_ctx* ctx, ch_npc_schedule_conditions** schedule_conditions)
{
    CH_CHECKED_ALLOC(*schedule_conditions, ch_arena_calloc(ctx->arena, sizeof **schedule_conditions));

    ch_str_ll** lists[] = {
        &(**schedule_conditions).conditions,
        &(**schedule_conditions).custom_interrupts,
        &(**schedule_conditions).pre_ignore,
        &(**schedule_conditions).ignore,
    };

    ch_byte_reader* br = &ctx->br;
    ch_record block;
    CH_RET_IF_ERR(ch_br_start_record(&ctx->st, br, &block));

    for (size_t i = 0; i < CH_ARRAYSIZE(lists); i++) {
        for (ch_str_ll** ll = lists[i];; ll = &(**ll).next) {
            size_t len = ch_br_strlen(br);
            if (len == 0)
                break;
            CH_CHECKED_ALLOC(*ll, ch_arena_calloc(ctx->arena, sizeof(**ll) + len + 1));
            (**ll).str = (char*)(*ll + 1);
            ch_br_read(br, (**ll).str, len);
            ch_br_skip_capped(br, 1);
        }
        ch_br_skip_capped(br, 1);
    }

    return ch_br_end_record(br, &block, true);
}

static ch_err ch_restore_navigator(ch_parsed_save_ctx* ctx, ch_npc_navigator** navigator)
{
    CH_CHECKED_ALLOC(*navigator, ch_arena_calloc(ctx->arena, sizeof **navigator));
    ch_record block;
    CH_RET_IF_ERR(ch_br_start_record(&ctx->st, &ctx->br, &block));
    (**navigator).version = ch_br_read_16(&ctx->br);
    bool ok = (**navigator).version == CH_NAVIGATOR_SAVE_VERSION;

    ch_datamap* dm = NULL;
    ch_err err = CH_ERR_NONE;
    if (ok) {
        err = ch_lookup_datamap(ctx, "AI_Waypoint_t", &dm);
        ok = !err;
    }
    if (ok) {
        err = ch_cr_utl_vector_restore(ctx, FIELD_EMBEDDED, dm, &(**navigator).path_vec);
        ok = !err;
    }
    if (err == CH_ERR_OUT_OF_MEMORY)
        return err;

    return ch_br_end_record(&ctx->br, &block, false);
}

// TODO should be possible to check the vtable of entities and check if there's some custom restore funcs, probably too much effort...
// TODO liquid portals lololol
static ch_err ch_restore_entity(ch_parsed_save_ctx* ctx, const char* classname, ch_restored_entity** pp_ent)
{
    // CEntitySaveRestoreBlockHandler::RestoreEntity

    CH_CHECKED_ALLOC(*pp_ent, ch_arena_calloc(ctx->arena, sizeof **pp_ent));
    ch_restored_entity* ent = *pp_ent;
    ent->classname = classname;
    CH_RET_IF_ERR(ch_lookup_datamap(ctx, classname, &ent->class_info.dm));

    if (ch_dm_inherts_from(ent->class_info.dm, "CAI_BaseNPC")) {
        // CAI_BaseNPC::Restore
        CH_CHECKED_ALLOC(ent->npc_header, ch_arena_calloc(ctx->arena, sizeof(*ent->npc_header)));
        CH_RET_IF_ERR(
            ch_br_restore_class_by_name(ctx, NULL, "AIExtendedSaveHeader_t", &ent->npc_header->extended_header));
        ch_type_description* td_version;
        CH_RET_IF_ERR(ch_find_field(ent->npc_header->extended_header.dm, "version", true, &td_version));
        int16_t version = CH_FIELD_AT(ent->npc_header->extended_header.data, td_version, int16_t);
        if (version >= CH_HEADER_AI_FIRST_VERSION_WITH_CONDITIONS)
            CH_RET_IF_ERR(ch_restore_conditions(ctx, &ent->npc_header->schedule_conditions));
        if (version >= CH_HEADER_AI_FIRST_VERSION_WITH_NAVIGATOR_SAVE)
            CH_RET_IF_ERR(ch_restore_navigator(ctx, &ent->npc_header->navigator));
    }

    CH_CHECKED_ALLOC(ent->class_info.data, ch_arena_calloc(ctx->arena, ent->class_info.dm->ch_size));
    CH_RET_IF_ERR(ch_br_restore_recursive(ctx, ent->class_info.dm, ent->class_info.data));
    // TODO CBaseEntity fixups

    // TODO speaker

    return CH_ERR_NONE;
}

ch_err ch_parse_entity_block_header(ch_parsed_save_ctx* ctx, ch_block_entities* block)
{
    // CEntitySaveRestoreBlockHandler::ReadRestoreHeaders

    ch_restored_class_arr* ent_table = &block->entity_table;

    ent_table->n_elems = ch_br_read_32(&ctx->br);
    if (ctx->br.overflowed)
        return CH_ERR_READER_OVERFLOWED;

    CH_RET_IF_ERR(ch_lookup_datamap(ctx, "entitytable_t", &ent_table->dm));

    CH_CHECKED_ALLOC(ent_table->data, ch_arena_calloc(ctx->arena, CH_RCA_DATA_SIZE(*ent_table)));

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

ch_err ch_parse_entity_block_body(ch_parsed_save_ctx* ctx, ch_block_entities* block)
{
    // CEntitySaveRestoreBlockHandler::Restore

    const ch_datamap* dm_ent_table = block->entity_table.dm;

    CH_CHECKED_ALLOC(block->entities,
                     ch_arena_calloc(ctx->arena, sizeof(ch_restored_entity*) * block->entity_table.n_elems));

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
        else if (err && err != CH_ERR_DATAMAP_NOT_FOUND)
            CH_PARSER_LOG_ERR(ctx, "'ch_restore_entity' failed: %s", ch_err_strs[err]);
    }
    return CH_ERR_NONE;
}
