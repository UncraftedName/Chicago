#pragma once

struct ch_parsed_save_ctx;
enum ch_err;

typedef struct ch_block_ents {
    int n_ents;
    unsigned char* ent_table; // array of entitytable_t
} ch_block_ents;

enum ch_err ch_parse_entity_header(struct ch_parsed_save_ctx* ctx);
