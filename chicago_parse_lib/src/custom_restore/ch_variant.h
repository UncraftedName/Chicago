#pragma once

#include <inttypes.h>

#include "ch_save_internal.h"

typedef struct ch_cr_variant {
    ch_field_type ft;
    union {
        char val_bool;
        char* val_str;
        int32_t val_i32;
        float val_f32;
        float val_vec3f[3];
        uint32_t val_rgba;
        uint32_t val_ehandle;
    };
} ch_cr_variant;

ch_err ch_cr_variant_restore(ch_parsed_save_ctx* ctx, ch_cr_variant** data);
