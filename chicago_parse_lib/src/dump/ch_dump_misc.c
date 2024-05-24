#include <inttypes.h>

#include "ch_dump_decl.h"

static ch_err ch_dump_tag_text(ch_dump_text* dump, const ch_tag* tag)
{
    return ch_dump_text_printf(dump, "tag: \"%.4s\", (version: %" PRIu32 ")\n", tag->id, tag->version);
}

const ch_dump_tag_fns g_dump_tag_fns = {
    .text = ch_dump_tag_text,
};

// TODO add an option to dump to array or to bullet list so we can combine the functionality from the save errors
static ch_err ch_dump_str_ll_text(ch_dump_text* dump, const ch_str_ll* ll)
{
    CH_RET_IF_ERR(ch_dump_text_printf(dump, "["));
    for (const ch_str_ll* cur = ll; cur; cur = cur->next) {
        if (cur->str)
            CH_RET_IF_ERR(ch_dump_text_printf(dump, "\"%s\"%s", cur->str, cur->next ? ", " : ""));
        else
            CH_RET_IF_ERR(ch_dump_text_printf(dump, "<null>%s", cur->next ? ", " : ""));
    }
    return ch_dump_text_printf(dump, "]");
}

const ch_dump_str_ll_fns g_dump_str_ll_fns = {
    .text = ch_dump_str_ll_text,
};
