#pragma once

#include "ch_dump.h"

// THE SAVE

CH_DECLARE_DUMP_FNS(sav, g_dump_sav_fns, const ch_parsed_save_data* save_data);

// misc stuff

CH_DECLARE_DUMP_FNS(tag, g_dump_tag_fns, const ch_tag* tag);
CH_DECLARE_DUMP_FNS(str_ll, g_dump_str_ll_fns, const ch_str_ll* ll);
CH_DECLARE_DUMP_FNS(restored_class, g_dump_restored_class_fns, const ch_datamap* dm, const unsigned char* data);

ch_err ch_dump_field_val_text(ch_dump_text* dump,
                              ch_field_type ft,
                              size_t total_size_bytes,
                              const unsigned char* field_ptr);

// state files

CH_DECLARE_DUMP_FNS(hl1, g_dump_hl1_fns, const ch_sf_save_data* sf);
CH_DECLARE_DUMP_FNS(hl2, g_dump_hl2_fns, const ch_sf_adjacent_client_state* sf);
CH_DECLARE_DUMP_FNS(hl3, g_dump_hl3_fns, const ch_sf_entity_patch* sf);

// custom fields

CH_DECLARE_DUMP_FNS(utl_vec,
                    g_dump_cr_utl_vec_fns,
                    const char* vec_name,
                    const struct ch_cr_utl_vector_restored* utl_vec);
