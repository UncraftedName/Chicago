#include "ch_save_internal.h"

#include <string.h>

// .hl2 info right after the tag
typedef struct ch_hl2_sections {

    int32_t entity_size_bytes;
    int32_t header_size_bytes;
    int32_t decal_size_bytes;
    int32_t music_size_bytes;
    int32_t symbol_size_bytes;

    int32_t decal_count;
    int32_t music_count;
    int32_t symbol_count;

} ch_hl2_sections;

#define CH_SECTION_MAGIC_NUMBER 0x54541234
#define CH_SECTION_VERSION_NUMBER 2

ch_err ch_parse_hl2(ch_parsed_save_ctx* ctx, ch_sf_adjacent_client_state* sf)
{
    // CSaveRestore::RestoreClientState

    ch_byte_reader* br = &ctx->br;

    ch_br_read(br, &sf->tag, sizeof sf->tag);
    const ch_tag expected_tag = {.id = {'V', 'A', 'L', 'V'}, .version = 0x73};
    if (memcmp(&sf->tag, &expected_tag, sizeof expected_tag))
        return CC_ERR_HL2_BAD_TAG;

    ch_hl2_sections sections = {0};

    if (ch_br_peak_u32(br) == CH_SECTION_MAGIC_NUMBER) {
        ch_br_skip(br, sizeof(uint32_t));
        if (ch_br_read_32(br) != CH_SECTION_VERSION_NUMBER)
            return CC_ERR_HL2_BAD_SECTION_HEADER;
        if (!ch_br_read(br, &sections, sizeof sections))
            return CH_ERR_READER_OVERFLOWED;
    } else {
        sections.entity_size_bytes = ch_br_read_32(br);
        sections.header_size_bytes = ch_br_read_32(br);
        sections.decal_size_bytes = ch_br_read_32(br);
        sections.symbol_size_bytes = ch_br_read_32(br);
        sections.decal_count = ch_br_read_32(br);
        sections.symbol_count = ch_br_read_32(br);
    }

    return CH_ERR_NONE;
}
