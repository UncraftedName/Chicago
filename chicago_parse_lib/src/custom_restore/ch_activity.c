#include "ch_activity.h"
#include "ch_field_reader.h"

ch_err ch_cr_activity_restore(ch_parsed_save_ctx* ctx, ch_cr_activity** data, const ch_type_description* td)
{
    // CActivityDataOps::Restore

    (void)td;

    CH_CHECKED_ALLOC(*data, ch_arena_calloc(ctx->arena, sizeof **data));
    ch_cr_activity* act = *data;
    act->index = ch_br_read_32(&ctx->br);
    if ((act->index & CH_ACTIVITY_FILE_TAG_MASK) == CH_ACTIVITY_FILE_TAG)
        CH_RET_IF_ERR(ch_br_read_str_n(&ctx->br, ctx->arena, &act->name, act->index & 0xffff));
    return CH_ERR_NONE;
}
