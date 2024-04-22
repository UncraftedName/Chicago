#pragma once

#include "datamap.h"

static const ch_type_description game_header_fields[] = {
    {.type = FIELD_CHARACTER, .name = "mapName", .flags = FTYPEDESC_SAVE},
    {.type = FIELD_CHARACTER, .name = "comment", .flags = FTYPEDESC_SAVE},
    {.type = FIELD_INTEGER, .name = "mapCount", .flags = FTYPEDESC_SAVE},
    {.type = FIELD_CHARACTER, .name = "originMapName", .flags = FTYPEDESC_SAVE},
    {.type = FIELD_CHARACTER, .name = "landmark", .flags = FTYPEDESC_SAVE},
};

static const ch_datamap game_header_map = {
    .fields = game_header_fields,
    .n_fields = sizeof(game_header_fields) / sizeof(*game_header_fields),
    .class_name = "GAME_HEADER",
};
