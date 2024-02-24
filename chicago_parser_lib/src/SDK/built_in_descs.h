#pragma once

#include "datamap.h"

static const ch_type_description game_header_fields[] = {
    {.type = FIELD_CHARACTER, .name = "mapName", .n_elems = 32, .flags = FTYPEDESC_SAVE, .field_size_bytes = 32},
    {.type = FIELD_CHARACTER, .name = "comment", .n_elems = 80, .flags = FTYPEDESC_SAVE, .field_size_bytes = 80},
    {.type = FIELD_INTEGER, .name = "mapCount", .n_elems = 1, .flags = FTYPEDESC_SAVE, .field_size_bytes = 4},
    {.type = FIELD_CHARACTER, .name = "originMapName", .n_elems = 32, .flags = FTYPEDESC_SAVE, .field_size_bytes = 32},
    {.type = FIELD_CHARACTER, .name = "landmark", .n_elems = 256, .flags = FTYPEDESC_SAVE, .field_size_bytes = 256},
};

static const ch_datamap game_header_map = {
    .data_desc = game_header_fields,
    .n_fields = sizeof(game_header_fields) / sizeof(*game_header_fields),
    .class_name = "GAME_HEADER",
};
