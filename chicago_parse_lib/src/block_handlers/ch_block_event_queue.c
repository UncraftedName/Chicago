#include "ch_save_internal.h"
#include "ch_field_reader.h"

ch_err ch_parse_block_event_queue_header(ch_parsed_save_ctx* ctx, ch_block_event_queue* block)
{
    block->version = ch_br_read_16(&ctx->br);
    CH_RET_IF_BR_OVERFLOWED(&ctx->br);
    return CH_ERR_NONE;
}

ch_err ch_parse_block_event_queue_body(ch_parsed_save_ctx* ctx, ch_block_event_queue* block)
{
    if (block->version != CH_HEADER_EVENTQUEUE_SAVE_RESTORE_VERSION) {
        CH_PARSER_LOG_ERR(ctx, "funny event queue version: %d", block->version);
        return CH_ERR_UNSUPPORTED_BLOCK_VERSION;
    }

    CH_RET_IF_ERR(ch_br_restore_class_by_name(ctx, "EventQueue", "CEventQueue", &block->queue));

    CH_RET_IF_ERR(ch_lookup_datamap(ctx, "EventQueuePrioritizedEvent_t", &block->events.dm));
    const ch_type_description* td_n_elems;
    CH_RET_IF_ERR(ch_find_field_log_if_dne(ctx, block->queue.dm, "m_iListCount", true, &td_n_elems, FIELD_INTEGER));
    block->events.n_elems = CH_FIELD_AT(block->queue.data, td_n_elems, int32_t);
    CH_CHECKED_ALLOC(block->events.data,
                     ch_arena_calloc(ctx->arena, block->events.dm->ch_size * block->events.n_elems));
    // in game code this sorts the events by the fire time, I don't care much
    for (size_t i = 0; i < block->events.n_elems; i++)
        CH_RET_IF_ERR(ch_br_restore_fields(ctx, "PEvent", block->events.dm, CH_RCA_ELEM_DATA(block->events, i)));

    return CH_ERR_NONE;
}
