#include "ch_dump_decl.h"
#include "custom_restore/ch_ent_output.h"

static ch_err ch_dump_ent_output_text(ch_dump_text* dump, const char* var_name, const ch_cr_ent_output* ent_output)
{
    CH_RET_IF_ERR(ch_dump_text_printf(dump, "COutputEvent %s:\n", var_name));
    dump->indent_lvl++;
    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns,
                                    dump,
                                    ent_output->ent_output_val.dm,
                                    ent_output->ent_output_val.data));
    CH_RET_IF_ERR(ch_dump_text_printf(dump,
                                      "%zu event(s)%s\n",
                                      ent_output->actions.n_elems,
                                      ent_output->actions.n_elems > 0 ? ":" : ""));
    dump->indent_lvl++;
    for (size_t i = 0; i < ent_output->actions.n_elems; i++)
        CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns,
                                        dump,
                                        ent_output->actions.dm,
                                        CH_RCA_ELEM_DATA(ent_output->actions, i)));
    dump->indent_lvl -= 2;
    return CH_ERR_NONE;
}

const ch_dump_cr_ent_output_fns g_dump_cr_ent_output = {
    .text = ch_dump_ent_output_text,
};
