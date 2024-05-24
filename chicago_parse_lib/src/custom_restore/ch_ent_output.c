#include "ch_ent_output.h"
#include "ch_field_reader.h"

ch_err ch_cr_ent_output_restore(ch_parsed_save_ctx* ctx, ch_cr_ent_output** data, const ch_type_description* td)
{
    // CEventsSaveDataOps::Restore

    CH_CHECKED_ALLOC(*data, ch_arena_calloc(ctx->arena, sizeof **data));
    ch_cr_ent_output* eo = *data;
    if (td->n_elems == 0)
        return CH_ERR_NONE;
    if (td->n_elems > 1) {
        // TODO try setting multiple OnTrigger events for a single output
        CH_PARSER_LOG_ERR(ctx, "expected type desc for %s to have only 1 elem, got %d", td->name, td->n_elems);
    }

    if (!ctx->ent_outputs_cache.dm_base_ent_output || !ctx->ent_outputs_cache.dm_event_action) {
        CH_RET_IF_ERR(ch_lookup_datamap(ctx, "CBaseEntityOutput", &ctx->ent_outputs_cache.dm_base_ent_output));
        CH_RET_IF_ERR(ch_lookup_datamap(ctx, "CEventAction", &ctx->ent_outputs_cache.dm_event_action));
    }

    eo->actions.n_elems = ch_br_read_32(&ctx->br);
    eo->actions.dm = ctx->ent_outputs_cache.dm_event_action;

    eo->ent_output_val.dm = ctx->ent_outputs_cache.dm_base_ent_output;
    CH_CHECKED_ALLOC(eo->ent_output_val.data, ch_arena_calloc(ctx->arena, eo->ent_output_val.dm->ch_size));
    CH_RET_IF_ERR(ch_br_restore_fields(ctx, "Value", eo->ent_output_val.dm, eo->ent_output_val.data));

    CH_CHECKED_ALLOC(eo->actions.data, ch_arena_calloc(ctx->arena, CH_RCA_DATA_SIZE(eo->actions)));
    for (size_t i = 0; i < eo->actions.n_elems; i++)
        CH_RET_IF_ERR(ch_br_restore_fields(ctx, "EntityOutput", eo->actions.dm, CH_RCA_ELEM_DATA(eo->actions, i)));

    return CH_ERR_NONE;
}