#pragma once

#include "ch_dump.h"

CH_DECLARE_DUMP_FNS(sav, g_dump_sav_fns, const ch_parsed_save_data* save_data);
CH_DECLARE_DUMP_FNS(tag, g_dump_tag_fns, const ch_tag* tag);
CH_DECLARE_DUMP_FNS(restored_class, g_dump_restored_class_fns, const ch_datamap* dm, const unsigned char* data);
CH_DECLARE_DUMP_FNS(hl1, g_dump_hl1_fns, const ch_sf_save_data* sf_save_data);
CH_DECLARE_DUMP_FNS(hl2, g_dump_hl2_fns, const ch_sf_adjacent_client_state* sf_client_state);
CH_DECLARE_DUMP_FNS(hl3, g_dump_hl3_fns, const ch_sf_entity_patch* sf_entity_patch);
