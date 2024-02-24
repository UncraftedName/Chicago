#pragma once

#include "ch_save.h"

typedef struct cb_byte_writer {
    unsigned char *cur, *end;
    bool overflowed;
    char _pad[3];
} cb_byte_writer;

inline bool ch_bw_could_write(const cb_byte_writer* bw, size_t n)
{
    return !bw->overflowed && bw->cur + n <= bw->end;
}

#define CH_BW_DEFINE_PRIMITIVE_WRITE(func_name, type)   \
    inline ch_err func_name(cb_byte_writer* bw, type x) \
    {                                                   \
        if (ch_bw_could_write(bw, sizeof x)) {          \
            *(type*)bw->cur = x;                        \
            bw->cur += sizeof x;                        \
            return CH_ERR_NONE;                         \
        } else {                                        \
            bw->overflowed = true;                      \
            bw->cur = bw->end;                          \
            return CH_ERR_WRITER_OVERFLOWED;            \
        }                                               \
    }

CH_BW_DEFINE_PRIMITIVE_WRITE(ch_bw_write_32, int32_t)
