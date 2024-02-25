#include "ch_save_internal.h"

ch_err ch_parse_hl3(ch_parsed_save_ctx* ctx, ch_sf_entity_patch* sf)
{
    ch_byte_reader* br = &ctx->br;
    sf->n_patched_ents = ch_br_read_32(br);
    size_t size_bytes = sf->n_patched_ents * sizeof(int32_t);
    if (!ch_br_could_skip(br, size_bytes))
        return CH_ERR_READER_OVERFLOWED;
    if (!size_bytes)
        return CH_ERR_NONE;
    sf->patched_ents = malloc(size_bytes);
    if (!sf->patched_ents)
        return CH_ERR_OUT_OF_MEMORY;
    if (!ch_br_read(br, sf->patched_ents, size_bytes))
        return CH_ERR_READER_OVERFLOWED;
    return CH_ERR_NONE;
}
