#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <memory.h>

typedef struct ch_byte_reader {
    unsigned char *cur, *end;
    bool overflowed;
} ch_byte_reader;

inline bool ch_br_could_skip(ch_byte_reader* br, size_t n)
{
    return !br->overflowed && br->cur + n <= br->end;
}

inline void ch_br_skip(ch_byte_reader* br, size_t n)
{
    if (ch_br_could_skip(br, n)) {
        br->cur += n;
    } else {
        br->overflowed = true;
        br->cur = br->end;
    }
}

inline void ch_br_read(ch_byte_reader* br, void* dest, size_t n)
{
    if (ch_br_could_skip(br, n)) {
        memcpy(dest, br->cur, n);
        br->cur += n;
    } else {
        br->overflowed = true;
        br->cur = br->end;
    }
}

#define CH_BR_DEFINE_PRIMITIVE_READ(func_name, ret_type) \
    inline ret_type func_name(ch_byte_reader* br)        \
    {                                                    \
        if (ch_br_could_skip(br, sizeof(ret_type))) {    \
            ret_type tmp = *(ret_type*)br->cur;          \
            br->cur += sizeof(ret_type);                 \
            return tmp;                                  \
        } else {                                         \
            br->overflowed = true;                       \
            br->cur = br->end;                           \
            return (ret_type)0;                          \
        }                                                \
    }

CH_BR_DEFINE_PRIMITIVE_READ(ch_br_read_32, int32_t)
CH_BR_DEFINE_PRIMITIVE_READ(ch_br_read_u32, uint32_t)

const char* ch_br_read_str(ch_byte_reader* br);
