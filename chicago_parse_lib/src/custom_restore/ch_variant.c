#include "ch_variant.h"
#include "ch_field_reader.h"

ch_err ch_cr_variant_restore(ch_parsed_save_ctx* ctx, ch_cr_variant** data)
{
    // CVariantSaveDataOps::Restore

    CH_CHECKED_ALLOC(*data, ch_arena_calloc(ctx->arena, sizeof **data));
    ch_byte_reader* br = &ctx->br;
    ch_cr_variant* var = *data;
    var->ft = ch_br_read_32(br);

    ch_err err = CH_ERR_NONE;

#define _CH_READ_CASE(ft, dest)                                              \
    case ft:                                                                 \
        err = ch_br_restore_simple_field(ctx, &(dest), ft, 1, sizeof(dest)); \
        break

#pragma warning(push)
#pragma warning(disable : 4061)
    switch (var->ft) {
        _CH_READ_CASE(FIELD_BOOLEAN, var->val_bool);
        _CH_READ_CASE(FIELD_INTEGER, var->val_i32);
        _CH_READ_CASE(FIELD_FLOAT, var->val_f32);
        _CH_READ_CASE(FIELD_EHANDLE, var->val_ehandle);
        _CH_READ_CASE(FIELD_STRING, var->val_str);
        _CH_READ_CASE(FIELD_COLOR32, var->val_rgba);
        _CH_READ_CASE(FIELD_VECTOR, var->val_vec3f);
        _CH_READ_CASE(FIELD_POSITION_VECTOR, var->val_vec3f);
        case FIELD_VOID:
            if (br->overflowed)
                err = CH_ERR_READER_OVERFLOWED;
            break;
        default:
            var->ft = FIELD_VOID;
            CH_PARSER_LOG_ERR(ctx, "Bad type %d", var->ft);
            break;
    }
#pragma warning(pop)
    return err;
}
