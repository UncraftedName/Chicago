#include "ch_activity.h"

ch_err ch_cr_activity_restore(ch_parsed_save_ctx* ctx, ch_cr_activity** data, const ch_type_description* td)
{
    // CActivityDataOps::Restore

    (void)td;

    CH_CHECKED_ALLOC(*data, ch_arena_calloc(ctx->arena, sizeof **data));
    ch_cr_activity* act = *data;
    act->index = ch_br_read_32(&ctx->br);
    if ((act->index & CH_ACTIVITY_FILE_TAG_MASK) == CH_ACTIVITY_FILE_TAG) {
        size_t len = ch_br_strlen(&ctx->br);
        CH_CHECKED_ALLOC(act->name, ch_arena_alloc(ctx->arena, len + 1));
        ch_br_read(&ctx->br, act->name, len);
        act->name[len] = '\0';
        ch_br_skip_unchecked(&ctx->br, 1);
    }
    return CH_ERR_NONE;
}
