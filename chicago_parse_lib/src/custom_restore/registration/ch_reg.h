#pragma once

#include "SDK/datamap.h"
#include "ch_save.h"

struct ch_custom_builder;
struct ch_datamap_collection;
struct ch_datamap_collection_header;

typedef struct ch_register_info {
    struct ch_custom_builder* builder;
    const ch_custom_ops* ops;
    const char* ex_module_name;
    const char* ex_class_name;
    const char* ex_field_name;
} ch_register_info;

typedef ch_err (*ch_custom_register_cb)(ch_register_info* info);

typedef struct ch_register_params {
    ch_custom_register_cb cb;
    struct ch_custom_builder* builder;
    ch_datamap_collection_header* header;
    ch_datamap_collection* collection;
} ch_register_params;

typedef ch_err (*ch_custom_register)(ch_register_params* params);

ch_err ch_register_all(ch_datamap_collection_header* header, ch_datamap_collection* collection);
