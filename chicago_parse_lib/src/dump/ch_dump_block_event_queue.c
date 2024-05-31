#include "ch_dump_decl.h"

static ch_err ch_dump_block_event_queue_text(ch_dump_text* dump, const ch_block_event_queue* block)
{
    CH_RET_IF_ERR(ch_dump_text_printf(dump, "version: %d\n", block->version));
    if (block->version != CH_HEADER_EVENTQUEUE_SAVE_RESTORE_VERSION) {
        CH_DUMP_TEXT_LOG_ERR(dump, "funny event queue version: %d", block->version);
        return CH_ERR_NONE;
    }
    // TODO pretty this up I guess
    // currently this prints "class CBaseEntityOutput:\n ..."
    // I would like something like "<field_name> CBaseEntityOutput:\n ..." kind of like for embedded fields
    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns, dump, block->queue.dm, block->queue.data));
    CH_RET_IF_ERR(
        ch_dump_text_printf(dump, "%zu event(s)%s", block->events.n_elems, block->events.n_elems == 0 ? "\n" : ":\n"));
    dump->indent_lvl++;
    for (size_t i = 0; i < block->events.n_elems; i++) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "[%zu] ", i));
        CH_RET_IF_ERR(
            CH_DUMP_TEXT_CALL(g_dump_restored_class_fns, dump, block->events.dm, CH_RCA_ELEM_DATA(block->events, i)));
    }
    dump->indent_lvl--;
    return CH_ERR_NONE;
}

const ch_dump_block_fns g_dump_block_event_queue_fns = {
    .text = ch_dump_block_event_queue_text,
};
