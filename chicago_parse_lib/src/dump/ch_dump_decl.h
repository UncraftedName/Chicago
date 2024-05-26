#pragma once

#include "ch_dump.h"

// THE SAVE

CH_DECLARE_DUMP_FNS_SINGLE(sav, g_dump_sav_fns, const ch_parsed_save_data* save_data);

// misc stuff

CH_DECLARE_DUMP_FNS_SINGLE(tag, g_dump_tag_fns, const ch_tag* tag);
CH_DECLARE_DUMP_FNS_SINGLE(str_ll, g_dump_str_ll_fns, const ch_str_ll* ll);
CH_DECLARE_DUMP_FNS_SINGLE(restored_class, g_dump_restored_class_fns, const ch_datamap* dm, const unsigned char* data);

ch_err ch_dump_field_val_text(ch_dump_text* dump,
                              ch_field_type ft,
                              size_t total_size_bytes,
                              const void* field_ptr,
                              bool always_show_as_array);

// state files

CH_DECLARE_DUMP_FNS_SINGLE(hl1, g_dump_hl1_fns, const ch_sf_save_data* sf);
CH_DECLARE_DUMP_FNS_SINGLE(hl2, g_dump_hl2_fns, const ch_sf_adjacent_client_state* sf);
CH_DECLARE_DUMP_FNS_SINGLE(hl3, g_dump_hl3_fns, const ch_sf_entity_patch* sf);

CH_DECLARE_DUMP_FNS(block, const void* block);
extern const ch_dump_block_fns g_dump_block_ents_fns, g_dump_block_physics_fns, g_dump_block_ai_fns,
    g_dump_block_templates_fns, g_dump_block_response_system_fns, g_dump_block_commentary_fns,
    g_dump_block_event_queue_fns, g_dump_block_achievements_fns;

// custom fields

CH_DECLARE_DUMP_FNS(custom, const ch_type_description* td, const void* restored_field);

struct ch_cr_utl_vector;
CH_DECLARE_DUMP_FNS_SINGLE(cr_utl_vec,
                           g_dump_cr_utl_vec_fns,
                           const char* var_name,
                           const struct ch_cr_utl_vector* utl_vec);

struct ch_cr_ent_output;
CH_DECLARE_DUMP_FNS_SINGLE(cr_ent_output,
                           g_dump_cr_ent_output,
                           const char* var_name,
                           const struct ch_cr_ent_output* ent_output);

struct ch_cr_variant;
CH_DECLARE_DUMP_FNS_SINGLE(cr_variant, g_dump_cr_variant, const char* var_name, const struct ch_cr_variant* variant);
