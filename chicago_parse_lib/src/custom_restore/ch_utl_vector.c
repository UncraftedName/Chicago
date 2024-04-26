#include "ch_utl_vector.h"
#include "ch_field_reader.h"

ch_err ch_cr_utl_vector_restore_to(ch_parsed_save_ctx* ctx,
                                   ch_field_type field_type,
                                   const ch_datamap* embedded_map,
                                   ch_cr_utl_vector_restored* vec)
{
    // CUtlVectorDataOps::Restore

    memset(vec, 0, sizeof *vec);

    assert(!embedded_map ^ (field_type == FIELD_EMBEDDED));
    vec->n_elems = ch_br_read_u32(&ctx->br);
    if (ctx->br.overflowed)
        return CH_ERR_READER_OVERFLOWED;

    size_t elem_size = field_type == FIELD_EMBEDDED ? embedded_map->ch_size : ch_field_type_byte_size(field_type);
    vec->elems = ch_arena_calloc(ctx->arena, elem_size * vec->n_elems);
    if (!vec->elems)
        return CH_ERR_OUT_OF_MEMORY;

    vec->vec_td.embedded_map = embedded_map;
    vec->vec_td.type = field_type;
    vec->vec_td.flags = FTYPEDESC_SAVE;
    vec->vec_td.name = "elems";

    vec->vec_dm.class_name = "uv";
    vec->vec_dm.fields = &vec->vec_td;
    vec->vec_dm.n_fields = 1;

    if (field_type == FIELD_EMBEDDED) {
        vec->vec_td.total_size_bytes = elem_size;
        for (uint32_t i = 0; i < vec->n_elems; i++) {
            ch_err err = ch_br_restore_recursive(ctx, &vec->vec_dm, vec->elems + elem_size * i);
            if (err)
                return err;
        }
        return CH_ERR_NONE;
    } else {
        vec->vec_td.total_size_bytes = elem_size * vec->n_elems;
        return ch_br_restore_fields(ctx, "elems", &vec->vec_dm, vec->elems);
    }
}

ch_err ch_cr_utl_vector_restore(ch_parsed_save_ctx* ctx,
                                ch_field_type field_type,
                                const ch_datamap* embedded_map,
                                ch_cr_utl_vector_restored** data)
{
    *data = ch_arena_alloc(ctx->arena, sizeof **data);
    if (!*data)
        return CH_ERR_OUT_OF_MEMORY;
    return ch_cr_utl_vector_restore_to(ctx, field_type, embedded_map, *data);
}

ch_err ch_cr_utl_vector_restore_by_name_to(ch_parsed_save_ctx* ctx, const char* name, ch_cr_utl_vector_restored* vec)
{
    ch_datamap* dm = NULL;
    ch_err err = ch_lookup_datamap(ctx, name, &dm);
    if (err)
        return err;
    err = ch_cr_utl_vector_restore_to(ctx, FIELD_EMBEDDED, dm, vec);
    vec->embedded_map = dm; // assign later cuz restore_to zeroes out vec
    return err;
}
