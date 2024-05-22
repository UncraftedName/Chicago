#include "ch_reg_decl.h"
#include "custom_restore/ch_utl_vector.h"
#include "dump/ch_dump_fns.h"

static ch_err ch_cr_utl_vector_dump_text(ch_dump_text* dump,
                                         const ch_type_description* td,
                                         const ch_cr_utl_vector_restored* vec)
{
    return CH_DUMP_TEXT_CALL(g_dump_cr_utl_vec_fns, dump, td->name, vec);
}

#define CH_DEFINE_CUSTOM_VECTOR_CB(ft)                                         \
    static ch_err _ch_cr_utl_vec_restore_##ft(ch_parsed_save_ctx* ctx,         \
                                              ch_cr_utl_vector_restored** vec, \
                                              const ch_datamap* dm)            \
    {                                                                          \
        return ch_cr_utl_vector_restore(ctx, ft, dm, vec);                     \
    }

CH_DEFINE_CUSTOM_VECTOR_CB(FIELD_EMBEDDED);
CH_DEFINE_CUSTOM_VECTOR_CB(FIELD_EHANDLE);

CH_DEFINE_REGISTER_FUNC(ch_utl_vec_register)
{
    const static ch_dump_custom_fns vector_dump_fns = {
        .text = ch_cr_utl_vector_dump_text,
    };

    ch_datamap_lookup_entry entry = {.name = "thinkfunc_t"};
    const ch_datamap_lookup_entry* dm_entry = hashmap_get(params->collection->lookup, &entry);
    if (dm_entry) {
        static ch_custom_ops ops;
        ops.dump_fns = &vector_dump_fns;
        ops.restore_fn = (ch_restore_custom)_ch_cr_utl_vec_restore_FIELD_EMBEDDED;
        ops.user_data = (void*)dm_entry->datamap;
        ch_register_info info = {
            .builder = params->builder,
            .ops = &ops,
            .ex_module_name = "server.dll",
            .ex_class_name = "CBaseEntity",
            .ex_field_name = "m_aThinkFunctions",
        };
        CH_RET_IF_ERR(params->cb(&info));
    }

    {
        static ch_custom_ops ops;
        ops.dump_fns = &vector_dump_fns;
        ops.restore_fn = (ch_restore_custom)_ch_cr_utl_vec_restore_FIELD_EHANDLE;
        ops.user_data = NULL;
        ch_register_info info = {
            .builder = params->builder,
            .ops = &ops,
            .ex_module_name = "server.dll",
            .ex_class_name = "CSceneEntity",
            .ex_field_name = "m_hActorList",
        };
        CH_RET_IF_ERR(params->cb(&info));
    }

    return CH_ERR_NONE;
}