#pragma once

#include "ch_save_internal.h"

#define CH_ACTIVITY_FILE_TAG_MASK 0xFFFF0000
#define CH_ACTIVITY_FILE_TAG 0x80800000

typedef struct ch_cr_activity {
    // if not equal to tag when masked, this is an index, otherwise irrelevant
    int32_t index;
    char* name;
} ch_cr_activity;

ch_err ch_cr_activity_restore(ch_parsed_save_ctx* ctx, ch_cr_activity** data, const ch_type_description* td);
