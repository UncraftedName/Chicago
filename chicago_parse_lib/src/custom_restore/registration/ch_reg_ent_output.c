#include "ch_reg_decl.h"
#include "custom_restore/ch_ent_output.h"
#include "dump/ch_dump_decl.h"

static ch_err ch_cr_ent_output_dump_text(ch_dump_text* dump,
                                         const ch_type_description* td,
                                         const ch_cr_ent_output* output)
{
    (void)td;
    return CH_DUMP_TEXT_CALL(g_dump_cr_ent_output, dump, td->name, output);
}

static ch_err _ch_cr_ent_output_restore(ch_parsed_save_ctx* ctx,
                                        ch_cr_ent_output** output,
                                        const ch_type_description* td,
                                        void* user_data)
{
    (void)user_data;
    return ch_cr_ent_output_restore(ctx, output, td);
}

CH_DEFINE_REGISTER_FUNC(ch_reg_ent_output)
{
    const static ch_dump_custom_fns dump_fns = {
        .text = ch_cr_ent_output_dump_text,
    };

    static ch_custom_ops ops;
    ops.dump_fns = &dump_fns;
    ops.restore_fn = (ch_restore_custom)_ch_cr_ent_output_restore;
    ops.user_data = NULL;
    ch_register_info info = {
        .builder = params->builder,
        .ops = &ops,
        .ex_module_name = "server.dll",
        .ex_class_name = "CBaseEntity",
        .ex_field_name = "m_OnUser1",
    };
    CH_RET_IF_ERR(params->cb(&info));

    return CH_ERR_NONE;
}
