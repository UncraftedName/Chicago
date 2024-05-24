#include <inttypes.h>

#include "ch_dump_decl.h"

static ch_err ch_dump_hl1_text(ch_dump_text* dump, const ch_sf_save_data* sf)
{

    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_tag_fns, dump, &sf->tag));
    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns, dump, sf->save_header.dm, sf->save_header.data));

    if (sf->adjacent_levels.n_elems > 0) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "%" PRId32 " adjacent Levels:\n", sf->adjacent_levels.n_elems));
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

    CH_RET_IF_ERR(ch_dump_text_printf(dump, "block \"Entities\":\n"));
    dump->indent_lvl++;

    const ch_block_entities* ent_block = &sf->blocks.entities;

    ch_type_description* td_classname;
    CH_RET_IF_ERR(
        ch_find_field_log_if_dne(NULL, ent_block->entity_table.dm, "classname", true, &td_classname, FIELD_STRING));

    for (size_t i = 0; i < ent_block->entity_table.n_elems; i++) {
        const ch_restored_entity* ent = ent_block->entities[i];
        if (!ent)
            continue;
        const char* classname = CH_FIELD_AT(CH_RCA_ELEM_DATA(ent_block->entity_table, i), td_classname, const char*);
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "entity \"%s\":\n", classname));
        dump->indent_lvl++;
        CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns,
                                        dump,
                                        ent_block->entity_table.dm,
                                        CH_RCA_ELEM_DATA(ent_block->entity_table, i)));
        if (ent->npc_header) {
            CH_RET_IF_ERR(ch_dump_text_printf(dump, "extra data for CAI_BaseNPC:\n"));
            dump->indent_lvl++;
            CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns,
                                            dump,
                                            ent->npc_header->extended_header.dm,
                                            ent->npc_header->extended_header.data));
            const ch_npc_schedule_conditions* conds = ent->npc_header->schedule_conditions;
            if (conds) {
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "conditions:\n"));
                dump->indent_lvl++;
                struct {
                    const char* name;
                    const ch_str_ll* ll;
                } cond_info[] = {
                    {.name = "m_Conditions", .ll = conds->conditions},
                    {.name = "m_CustomInterruptConditions", .ll = conds->custom_interrupts},
                    {.name = "m_ConditionsPreIgnore", .ll = conds->pre_ignore},
                    {.name = "m_IgnoreConditions", .ll = conds->ignore},
                };
                for (size_t j = 0; j < CH_ARRAYSIZE(cond_info); j++) {
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s: ", cond_info[j].name));
                    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_str_ll_fns, dump, cond_info[j].ll));
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "\n"));
                }
                dump->indent_lvl--;
            }
            const ch_npc_navigator* nav = ent->npc_header->navigator;
            if (nav) {
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "CAI_Navigator:\n"));
                dump->indent_lvl++;
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "version: %" PRId16 "\n", nav->version));
                if (nav->path_vec)
                    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_cr_utl_vec_fns, dump, "minPathName", nav->path_vec));
                dump->indent_lvl--;
            }
            dump->indent_lvl--;
        }
        CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns, dump, ent->class_info.dm, ent->class_info.data));
        dump->indent_lvl--;
    }
    dump->indent_lvl--;

    return CH_ERR_NONE;
}

const ch_dump_hl1_fns g_dump_hl1_fns = {
    .text = ch_dump_hl1_text,
};
