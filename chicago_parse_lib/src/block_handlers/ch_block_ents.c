#include "ch_save_internal.h"
#include "ch_field_reader.h"
#include "custom_restore/ch_utl_vector.h"
#include "ch_block_ents.h"

ch_err ch_parse_entity_header(ch_parsed_save_ctx* ctx)
{
    ch_parse_save_log_error(ctx, "hello from entity handler");
    ch_block_ents* block = &ctx->block_ents;
    block->n_ents = ch_br_read_32(&ctx->br);
    if (ctx->br.overflowed)
        return CH_ERR_READER_OVERFLOWED;

    const ch_datamap* dm;
    CH_RET_IF_ERR(ch_lookup_datamap(ctx, "entitytable_t", &dm));

    block->ent_table = ch_arena_calloc(ctx->arena, block->n_ents * dm->ch_size);
    if (!block->ent_table)
        return CH_ERR_OUT_OF_MEMORY;

    const ch_type_description* td_name;
    ch_find_field_log_if_dne(ctx, dm, "classname", true, &td_name);

    // initialize some int fields w/ -1
    // the game also inits restoreentityindex but that's not part of the datamap
    const char* init_fields[] = {"id", "edictindex", "saveentityindex"};
    for (size_t i = 0; i < CH_ARRAYSIZE(init_fields); i++) {
        const ch_type_description* td;
        ch_find_field_log_if_dne(ctx, dm, init_fields[i], true, &td);
        for (int j = 0; td && j < block->n_ents; j++)
            CH_FIELD_AT(block->ent_table + dm->ch_size * j, td, int) = -1;
    }

    for (int i = 0; i < block->n_ents; i++) {
        unsigned char* ent_entry = block->ent_table + dm->ch_size * i;
        CH_RET_IF_ERR(ch_br_restore_fields(ctx, "ETABLE", dm, ent_entry));
        printf("entity: %s\n", CH_FIELD_AT(ent_entry, td_name, const char*));
    }

    return CH_ERR_NONE;
}
