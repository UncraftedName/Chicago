#include <inttypes.h>

#include "ch_dump_decl.h"
#include "custom_restore/ch_utl_vector.h"

static ch_err ch_dump_hl1_text(ch_dump_text* dump, const ch_sf_save_data* sf)
{
    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_tag_fns, dump, &sf->tag));
    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns, dump, sf->save_header.dm, sf->save_header.data));

    if (sf->adjacent_levels.n_elems > 0) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "%" PRId32 " adjacent levels:\n", sf->adjacent_levels.n_elems));
        dump->indent_lvl++;
    } else {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "No adjacent levels.\n"));
    }

    for (size_t i = 0; i < sf->adjacent_levels.n_elems; i++) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "[%" PRId32 "] ", i));
        CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns,
                                        dump,
                                        sf->adjacent_levels.dm,
                                        CH_RCA_ELEM_DATA(sf->adjacent_levels, i)));
    }
    if (sf->adjacent_levels.n_elems > 0)
        dump->indent_lvl--;

    if (sf->light_styles.n_elems > 0) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "%" PRId32 " light styles:\n", sf->light_styles.n_elems));
        dump->indent_lvl++;
    } else {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "No light styles."));
    }

    for (size_t i = 0; i < sf->light_styles.n_elems; i++) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "[%" PRId32 "] ", i));
        CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns,
                                        dump,
                                        sf->light_styles.dm,
                                        CH_RCA_ELEM_DATA(sf->light_styles, i)));
    }

    if (sf->light_styles.n_elems > 0)
        dump->indent_lvl--;

    CH_RET_IF_ERR(ch_dump_text_printf(dump, "block(s):\n"));
    dump->indent_lvl++;

    ch_type_description* td_name = NULL;
    CH_RET_IF_ERR(
        ch_find_field_log_if_dne(NULL, sf->block_headers->embedded_map, "szName", true, &td_name, FIELD_CHARACTER));

    for (size_t i = 0; i < CH_BLOCK_COUNT; i++) {
        const ch_block* block = &sf->blocks[i];
        if (block->vec_idx == 0 || !block->header_parsed)
            continue;
        CH_RET_IF_ERR(ch_dump_text_printf(
            dump,
            "block \"%s\":\n",
            CH_FIELD_AT_PTR(CH_UTL_VEC_ELEM_PTR(*sf->block_headers, block->vec_idx - 1), td_name, const char)));
        dump->indent_lvl++;

#define _CH_BLOCK_CASE(type, fns)                                 \
    case type:                                                    \
        CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(fns, dump, block->data)); \
        break

        switch (i) {
            _CH_BLOCK_CASE(CH_BLOCK_ENTITIES, g_dump_block_ents_fns);
            _CH_BLOCK_CASE(CH_BLOCK_PHYSICS, g_dump_block_physics_fns);
            _CH_BLOCK_CASE(CH_BLOCK_AI, g_dump_block_ai_fns);
            _CH_BLOCK_CASE(CH_BLOCK_TEMPLATES, g_dump_block_templates_fns);
            _CH_BLOCK_CASE(CH_BLOCK_RESPONSE_SYSTEM, g_dump_block_response_system_fns);
            _CH_BLOCK_CASE(CH_BLOCK_COMMENTARY, g_dump_block_commentary_fns);
            _CH_BLOCK_CASE(CH_BLOCK_EVENT_QUEUE, g_dump_block_event_queue_fns);
            _CH_BLOCK_CASE(CH_BLOCK_ACHIEVEMENTS, g_dump_block_achievements_fns);
            default:
                assert(0);
                // TODO
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "AHHHHH\n"));
                break;
        }
        dump->indent_lvl--;
    }
    dump->indent_lvl--;
    return CH_ERR_NONE;
}

const ch_dump_hl1_fns g_dump_hl1_fns = {
    .text = ch_dump_hl1_text,
};
