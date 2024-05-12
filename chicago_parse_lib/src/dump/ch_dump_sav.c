#include <inttypes.h>

#include "ch_dump_fns.h"

// TODO yeah i'll need to check for nulls at every possible failure, fun :)
static ch_err ch_dump_sav_text(ch_dump_text* dump, ch_parsed_save_data* save_data)
{
    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_tag_fns, dump, &save_data->tag));
    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns, dump, &save_data->game_header));
    CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_restored_class_fns, dump, &save_data->global_state));
    for (size_t i = 0; i < save_data->n_state_files; i++) {
        ch_state_file* sf = &save_data->state_files[i];
        CH_RET_IF_ERR(ch_dump_text_printf(dump,
                                          "\nstate file \"%.*s\" (%s):\n",
                                          sizeof sf->name,
                                          sf->name,
                                          ch_sf_type_strs[sf->sf_type]));
        dump->indent_lvl++;
        switch (sf->sf_type) {
            case CH_SF_SAVE_DATA:
                CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_hl1_fns, dump, &sf->sf_save_data));
                break;
            case CH_SF_ADJACENT_CLIENT_STATE:
                CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_hl2_fns, dump, &sf->sf_adjacent_client_state));
                break;
            case CH_SF_ENTITY_PATCH:
                CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(g_dump_hl3_fns, dump, &sf->sf_entity_patch));
                break;
            case CH_SF_INVALID:
            default:
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "INVALID"));
                break;
        }
        dump->indent_lvl--;
    }

    if (save_data->errors_ll) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "\n\nErrors generated during parsing:\n"));
        dump->indent_lvl++;
        for (ch_parse_save_error* err = save_data->errors_ll; err; err = err->next)
            CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s\n", err->err_str));
        dump->indent_lvl--;
    }
    return CH_ERR_NONE;
}

static ch_err ch_dump_tag_text(ch_dump_text* dump, const ch_tag* tag)
{
    return ch_dump_text_printf(dump, "tag: \"%.4s\", (version: %" PRIu32 ")\n", tag->id, tag->version);
}

const ch_dump_fns g_dump_sav_fns = {
    .text = ch_dump_sav_text,
};

const ch_dump_fns g_dump_tag_fns = {
    .text = ch_dump_tag_text,
};
