#include "ch_save_internal.h"

#include <string.h>

ch_err ch_parse_sf_save_data(ch_parsed_save_ctx* ctx, ch_sf_save_data* sf)
{
    ch_br_read(&ctx->br, sf->id, sizeof sf->id);
    sf->version = ch_br_read_u32(&ctx->br);
    if (strncmp(sf->id, "VALV", 4) || sf->version != 0x73)
        return CC_ERR_HL1_INVALID_HEADER;
    return CH_ERR_NONE;
}
