#include "ch_save_internal.h"
#include "SDK/built_in_descs.h"

typedef struct ch_hl1_sections {
    int32_t symbol_table_size_bytes;
    int32_t n_symbols;
    int32_t data_headers_size_bytes;
    // TODO what the fuck is this?
    int32_t n_bytes_data;
} ch_hl1_sections;

ch_err ch_parse_hl1(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf)
{
    // CSaveRestore::LoadSaveData

    ch_byte_reader* br = &ctx->br;

    ch_br_read(br, &sf->tag, sizeof sf->tag);
    const ch_tag expected_tag = {.id = {'V', 'A', 'L', 'V'}, .version = 0x73};
    if (memcmp(&sf->tag, &expected_tag, sizeof expected_tag))
        return CC_ERR_HL1_BAD_TAG;

    ch_hl1_sections sections;
    if (!ch_br_read(br, &sections, sizeof sections))
        return CH_ERR_READER_OVERFLOWED;

    if (sections.symbol_table_size_bytes > 0) {
        ch_byte_reader br_st = ch_br_split_skip(br, sections.symbol_table_size_bytes);
        if (br->overflowed)
            return CH_ERR_READER_OVERFLOWED;
        ch_err err = ch_br_read_symbol_table(&br_st, &ctx->st, sections.n_symbols);
        if (err)
            return err;
    }

    // CSaveRestoreBlockSet::ReadRestoreHeaders

    int32_t headers_size_bytes = ch_br_read_32(br);
    int32_t bodies_size_bytes = ch_br_read_32(br);

    (void)headers_size_bytes;
    (void)bodies_size_bytes;

    return CH_ERR_NONE;
}
