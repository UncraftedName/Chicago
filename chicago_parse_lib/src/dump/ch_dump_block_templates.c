#include <inttypes.h>
#include <ctype.h>

#include "ch_dump_decl.h"

static ch_err ch_dump_block_templates_text(ch_dump_text* dump, const ch_block_templates* block)
{
    CH_RET_IF_ERR(ch_dump_text_printf(dump, "version: %d\n", block->version));
    if (block->version != CH_HEADER_TEMPLATE_SAVE_RESTORE_VERSION) {
        CH_DUMP_TEXT_LOG_ERR(dump, "funny templates version: %d", block->version);
        return CH_ERR_NONE;
    }

    CH_RET_IF_ERR(ch_dump_text_printf(dump,
                                      "template instance: %" PRId32 "\n%" PRId16 " template(s)%s",
                                      block->template_instance,
                                      block->n_templates,
                                      block->n_templates == 0 ? "\n" : ":\n"));
    dump->indent_lvl++;
    for (int16_t i = 0; i < block->n_templates; i++) {
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "template [%" PRId16 "]:\n", i));
        dump->indent_lvl++;
        CH_RET_IF_ERR(
            CH_DUMP_TEXT_CALL(g_dump_restored_class_fns, dump, block->dm_template, block->templates[i].template_data));
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "name: \"%s\"\n", block->templates[i].name));

        const char* map_data = block->templates[i].map_data;
        bool has_nl = false;
        bool has_gr = false;
        const char* c = map_data;
        for (; *c; c++) {
            has_nl |= *c == '\n';
            has_gr |= isgraph(*c);
        }

        if (has_nl) {
            CH_RET_IF_ERR(ch_dump_text_printf(dump, "map_data:\n"));
            dump->indent_lvl++;
            if (has_gr) {
                // skip leading & trailing newlines
                const char* from = map_data;
                for (; *from == '\n'; from++) {}
                const char* to = c;
                for (; *(to - 1) == '\n'; to--) {}
                assert(from < to);
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%.*s\n", (size_t)to - (size_t)from, from));
            } else {
                // only new lines & whitespace, just print the whole thing
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s\n", map_data));
            }
            dump->indent_lvl--;
        } else {
            // print in quotes if there's only whitespace
            if (has_gr)
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "map data: %s\n", map_data));
            else
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "map data: \"%s\"\n", map_data));
        }
        dump->indent_lvl--;
    }
    dump->indent_lvl--;
    return CH_ERR_NONE;
}

const ch_dump_block_fns g_dump_block_templates_fns = {
    .text = ch_dump_block_templates_text,
};
