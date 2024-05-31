#include <inttypes.h>

#include "ch_dump_decl.h"

static ch_err ch_dump_tag_text(ch_dump_text* dump, const ch_tag* tag)
{
    return ch_dump_text_printf(dump, "tag: \"%.4s\", (version: %" PRIu32 ")\n", tag->id, tag->version);
}

const ch_dump_tag_fns g_dump_tag_fns = {
    .text = ch_dump_tag_text,
};

static ch_err ch_dump_str_ll_text(ch_dump_text* dump, const ch_str_ll* ll, ch_dump_text_str_ll_type type)
{
    switch (type) {
        case CH_DUMP_TEXT_STR_LL_ARRAY_LIKE:
            CH_RET_IF_ERR(ch_dump_text_printf(dump, "["));
            for (const ch_str_ll* cur = ll; cur; cur = cur->next) {
                if (cur->str)
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "\"%s\"%s", cur->str, cur->next ? ", " : ""));
                else
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "<null>%s", cur->next ? ", " : ""));
            }
            return ch_dump_text_printf(dump, "]");
        case CH_DUMP_TEXT_STR_LL_NL_SEP:
            for (const ch_str_ll* cur = ll; cur; cur = cur->next) {
                if (cur->str)
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s%s", cur->str, cur->next ? "\n" : ""));
                else
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "<null>%s", cur->next ? "\n" : ""));
            }
            return CH_ERR_NONE;
        default:
            assert(0);
            CH_DUMP_TEXT_LOG_ERR(dump, "bad str_ll dump type: %d", type);
            return CH_ERR_NONE;
    }
}

const ch_dump_str_ll_fns g_dump_str_ll_fns = {
    .text = ch_dump_str_ll_text,
};
