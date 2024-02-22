#include <string.h>

#include "ch_save_internal.h"

ch_err ch_br_read_symbol_table(ch_byte_reader* br, ch_symbol_table* st, int n_symbols)
{
    if (n_symbols < 0)
        return CH_ERR_INVALID_SYMBOL_TABLE;
    st->symbols = (const char*)br->cur;
    st->n_symbols = n_symbols;
    st->symbol_offs = calloc(st->n_symbols, sizeof *st->symbol_offs);
    if (!st->symbol_offs)
        return CH_ERR_OUT_OF_MEMORY;

    const unsigned char* start = br->cur;
    for (int i = 0; i < n_symbols && !br->overflowed; i++) {
        if (*br->cur)
            st->symbol_offs[i] = br->cur - start;
        ch_br_skip(br, strnlen((const char*)br->cur, br->end - br->cur) + 1);
    }
    if (br->overflowed) {
        ch_free_symbol_table(st);
        return CH_ERR_INVALID_SYMBOL_TABLE; // being more specific than reader overflow
    } else {
        return CH_ERR_NONE;
    }
}
