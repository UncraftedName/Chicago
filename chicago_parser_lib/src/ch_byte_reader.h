#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct ch_byte_reader {
    unsigned char *cur, *end;
    bool overflowed;
} ch_byte_reader;

inline void ch_br_read(ch_byte_reader* br, void* dest, size_t n)
{
    if (br->cur + n > br->end) {
        br->overflowed = true;
        br->cur = br->end;
    } else {
        memcpy(dest, br->cur, n);
        br->cur += n;
    }
}

int32_t ch_br_read_32(ch_byte_reader* br);
uint32_t ch_br_read_u32(ch_byte_reader* br);
const char* ch_br_read_str(ch_byte_reader* br);
