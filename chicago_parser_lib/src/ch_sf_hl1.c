#include "ch_save_internal.h"

typedef struct ch_hl1_sections {
    int symbol_table_size_bytes;
    int num_symbols;
    // TODO what's the difference between these two?
    int data_headers_size_bytes;
    int n_bytes_data;
} ch_hl1_sections;

ch_err ch_parse_hl1(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf)
{
    // CSaveRestore::LoadSaveData

    ch_byte_reader* br = &ctx->br;

    ch_br_read(br, &sf->tag, sizeof sf->tag);
    const ch_tag expected_tag = {.id = {'V', 'A', 'L', 'V'}, .version = 0x73};
    if (memcmp(&sf->tag, &expected_tag, sizeof expected_tag))
        return CC_ERR_HL1_INVALID_TAG;

    ch_hl1_sections sections;
    ch_br_read(br, &sections, sizeof sections);

    return CH_ERR_NONE;
}
