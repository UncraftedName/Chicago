#pragma once

#include "ch_save_internal.h"

typedef struct ch_cr_utl_vector {
    uint32_t n_elems;
    unsigned char* elems;
    const ch_datamap* embedded_map;
    size_t elem_size;
    ch_field_type field_type;
} ch_cr_utl_vector;

#define CH_UTL_VEC_ELEM_PTR(utl_vec, i) \
    (assert((size_t)(i) < (utl_vec).n_elems), (utl_vec).elems + (utl_vec).elem_size * (i))

// TODO can this be removed? we should really have pointers to everything right?
ch_err ch_cr_utl_vector_restore_to(ch_parsed_save_ctx* ctx,
                                   ch_field_type field_type,
                                   const ch_datamap* embedded_map,
                                   ch_cr_utl_vector* vec);

ch_err ch_cr_utl_vector_restore(ch_parsed_save_ctx* ctx,
                                ch_field_type field_type,
                                const ch_datamap* embedded_map,
                                ch_cr_utl_vector** data);

ch_err ch_cr_utl_vector_restore_by_name_to(ch_parsed_save_ctx* ctx, const char* name, ch_cr_utl_vector* vec);
