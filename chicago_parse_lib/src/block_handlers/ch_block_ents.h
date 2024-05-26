#pragma once

#include "ch_save_internal.h"

#define CH_HEADER_AI_FIRST_VERSION_WITH_CONDITIONS 2
#define CH_HEADER_AI_FIRST_VERSION_WITH_NAVIGATOR_SAVE 5
#define CH_NAVIGATOR_SAVE_VERSION 1

ch_err ch_parse_entity_block_header(ch_parsed_save_ctx* ctx, ch_block_entities* block);
ch_err ch_parse_entity_block_body(ch_parsed_save_ctx* ctx, ch_block_entities* block);
