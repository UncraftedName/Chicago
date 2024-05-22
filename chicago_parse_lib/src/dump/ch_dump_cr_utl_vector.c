#include <inttypes.h>

#include "ch_dump_fns.h"
#include "custom_restore/ch_utl_vector.h"

// TODO idk come up with some way of handling null vectors
static ch_err ch_dump_utl_vec_text(ch_dump_text* dump, const char* vec_name, const ch_cr_utl_vector_restored* utl_vec)
{

    if (utl_vec->embedded_map) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump,
                                          "CUtlVector<%s,%zu> %s:%s",
                                          utl_vec->embedded_map->class_name,
                                          utl_vec->n_elems,
                                          vec_name,
                                          utl_vec->n_elems == 0 ? " []\n" : "\n"));
        dump->indent_lvl++;
        for (size_t i = 0; i < utl_vec->n_elems; i++) {
            CH_RET_IF_ERR(ch_dump_text_printf(dump, "[%zu] ", i));
            CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns,
                                            dump,
                                            utl_vec->embedded_map,
                                            CH_UTL_VEC_ELEM_PTR(*utl_vec, i)));
        }
        dump->indent_lvl--;
    } else {
        CH_RET_IF_ERR(ch_dump_text_printf(dump,
                                          "CUtlVector<%s,%zu> %s: ",
                                          ch_field_string(utl_vec->field_type),
                                          utl_vec->n_elems,
                                          vec_name));
        CH_RET_IF_ERR(ch_dump_field_val_text(dump,
                                             utl_vec->field_type,
                                             utl_vec->elem_size * utl_vec->n_elems,
                                             utl_vec->elems,
                                             true));
    }
    return CH_ERR_NONE;
}

const ch_dump_utl_vec_fns g_dump_cr_utl_vec_fns = {
    .text = ch_dump_utl_vec_text,
};
