#include <inttypes.h>

#include "ch_dump_decl.h"
#include "custom_restore/ch_activity.h"

static ch_err ch_dump_activity_text(ch_dump_text* dump, const char* var_name, const ch_cr_activity* activity)
{
    if ((activity->index & CH_ACTIVITY_FILE_TAG_MASK) == CH_ACTIVITY_FILE_TAG)
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "Activity %s: \"%s\"\n", var_name, activity->name));
    else
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "Activity %s: index %" PRId32 "\n", var_name, activity->index));
    return CH_ERR_NONE;
}

const ch_dump_cr_activity_fns g_dump_cr_activity = {
    .text = ch_dump_activity_text,
};
