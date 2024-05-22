#include "ch_utl_vector.h"
#include "ch_field_reader.h"
#include "dump/ch_dump_fns.h"

ch_err ch_cr_utl_vector_restore_to(ch_parsed_save_ctx* ctx,
                                   ch_field_type field_type,
                                   const ch_datamap* embedded_map,
                                   ch_cr_utl_vector_restored* vec)
{
    // CUtlVectorDataOps::Restore

    memset(vec, 0, sizeof *vec);
    vec->embedded_map = embedded_map;
    vec->field_type = field_type;

    assert(!embedded_map ^ (field_type == FIELD_EMBEDDED));
    vec->n_elems = ch_br_read_u32(&ctx->br);
    if (ctx->br.overflowed)
        return CH_ERR_READER_OVERFLOWED;

    vec->elem_size = field_type == FIELD_EMBEDDED ? embedded_map->ch_size : ch_field_type_byte_size(field_type);
    vec->elems = ch_arena_calloc(ctx->arena, vec->elem_size * vec->n_elems);
    if (!vec->elems)
        return CH_ERR_OUT_OF_MEMORY;

    ch_type_description vec_td = {
        .name = "elems",
        .embedded_map = embedded_map,
        .type = field_type,
        .flags = FTYPEDESC_SAVE,
    };

    ch_datamap vec_dm = {
        .class_name = "uv",
        .fields = &vec_td,
        .n_fields = 1,
    };

    if (field_type == FIELD_EMBEDDED) {
        vec_td.n_elems = 1;
        vec_td.total_size_bytes = embedded_map->ch_size;
        for (uint32_t i = 0; i < vec->n_elems; i++)
            CH_RET_IF_ERR(ch_br_restore_recursive(ctx, &vec_dm, CH_UTL_VEC_ELEM_PTR(*vec, i)));
        return CH_ERR_NONE;
    } else {
        vec_td.n_elems = (unsigned short)vec->n_elems;
        vec_td.total_size_bytes = vec->elem_size * vec->n_elems;
        return ch_br_restore_fields(ctx, "elems", &vec_dm, vec->elems);
    }
}

ch_err ch_cr_utl_vector_restore(ch_parsed_save_ctx* ctx,
                                ch_field_type field_type,
                                const ch_datamap* embedded_map,
                                ch_cr_utl_vector_restored** data)
{
    CH_CHECKED_ALLOC(*data, ch_arena_alloc(ctx->arena, sizeof **data));
    return ch_cr_utl_vector_restore_to(ctx, field_type, embedded_map, *data);
}

ch_err ch_cr_utl_vector_restore_by_name_to(ch_parsed_save_ctx* ctx, const char* name, ch_cr_utl_vector_restored* vec)
{
    ch_datamap* dm = NULL;
    CH_RET_IF_ERR(ch_lookup_datamap(ctx, name, &dm));
    return ch_cr_utl_vector_restore_to(ctx, FIELD_EMBEDDED, dm, vec);
}
