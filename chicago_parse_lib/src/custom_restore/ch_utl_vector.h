#pragma once

#include "ch_save_internal.h"

// TODO the vector td & dm are basically constant and not informative, they can be safely removed
// keep the embedded map pointer though & create a field for the type of vector
typedef struct ch_cr_utl_vector_restored {
    uint32_t n_elems;
    unsigned char* elems;
    ch_type_description vec_td;
    ch_datamap vec_dm;
    const ch_datamap* embedded_map;
} ch_cr_utl_vector_restored;

ch_err ch_cr_utl_vector_restore_to(ch_parsed_save_ctx* ctx,
                                   ch_field_type field_type,
                                   const ch_datamap* embedded_map,
                                   ch_cr_utl_vector_restored* vec);

ch_err ch_cr_utl_vector_restore(ch_parsed_save_ctx* ctx,
                                ch_field_type field_type,
                                const ch_datamap* embedded_map,
                                ch_cr_utl_vector_restored** data);

ch_err ch_cr_utl_vector_restore_by_name_to(ch_parsed_save_ctx* ctx, const char* name, ch_cr_utl_vector_restored* vec);
