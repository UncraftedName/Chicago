#include <inttypes.h>

#include "ch_dump_decl.h"

static ch_err ch_dump_block_ents_text(ch_dump_text* dump, const ch_block_entities* block)
{
    ch_type_description* td_classname = NULL;
    CH_RET_IF_ERR(
        ch_find_field_log_if_dne(NULL, block->entity_table.dm, "classname", true, &td_classname, FIELD_STRING));

    for (size_t i = 0; i < block->entity_table.n_elems; i++) {
        const ch_restored_entity* ent = block->entities[i];
        if (!ent)
            continue;
        const char* classname = CH_FIELD_AT(CH_RCA_ELEM_DATA(block->entity_table, i), td_classname, const char*);
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "entity \"%s\":\n", classname));
        dump->indent_lvl++;
        CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns,
                                        dump,
                                        block->entity_table.dm,
                                        CH_RCA_ELEM_DATA(block->entity_table, i)));
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

    return CH_ERR_NONE;
}

const ch_dump_block_fns g_dump_block_ents_fns = {
    .text = ch_dump_block_ents_text,
};
