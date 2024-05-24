#include "ch_dump_decl.h"
#include "custom_restore/ch_variant.h"

static ch_err ch_dump_variant_text(ch_dump_text* dump, const char* var_name, const ch_cr_variant* var)
{
    CH_RET_IF_ERR(ch_dump_text_printf(dump, "variant_t %s (%s): ", var_name, ch_field_type_string(var->ft)));

#define _CH_DUMP_CASE(ft, source, print_as_arr) \
    case ft:                                    \
        return ch_dump_field_val_text(dump, ft, sizeof(source), &(source), print_as_arr)

#pragma warning(push)
#pragma warning(disable : 4061)
    switch (var->ft) {
        _CH_DUMP_CASE(FIELD_BOOLEAN, var->val_bool, false);
        _CH_DUMP_CASE(FIELD_INTEGER, var->val_i32, false);
        _CH_DUMP_CASE(FIELD_FLOAT, var->val_f32, false);
        _CH_DUMP_CASE(FIELD_EHANDLE, var->val_ehandle, false);
        _CH_DUMP_CASE(FIELD_STRING, var->val_str, false);
        _CH_DUMP_CASE(FIELD_COLOR32, var->val_rgba, false);
        _CH_DUMP_CASE(FIELD_VECTOR, var->val_vec3f, true);
        _CH_DUMP_CASE(FIELD_POSITION_VECTOR, var->val_vec3f, true);
        case FIELD_VOID:
            return ch_dump_text_printf(dump, "EMPTY\n");
        default:
            assert(0); // TODO log stuff here
            return CH_ERR_NONE;
    }
#pragma warning(pop)
}

const ch_dump_cr_variant_fns g_dump_cr_variant = {
    .text = ch_dump_variant_text,
};
