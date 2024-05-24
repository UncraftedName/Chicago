#pragma once

#include "ch_save_internal.h"

typedef struct ch_cr_ent_output {
    ch_restored_class ent_output_val;
    ch_restored_class_arr actions;
} ch_cr_ent_output;

ch_err ch_cr_ent_output_restore(ch_parsed_save_ctx* ctx, ch_cr_ent_output** data, const ch_type_description* td);
