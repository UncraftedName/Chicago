#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct ch_byte_reader {
    unsigned char *cur, *end;
    bool overflowed;
} ch_byte_reader;

void ch_read(ch_byte_reader* r, void* dest, size_t n);
int32_t ch_read_32(ch_byte_reader* r);
uint32_t ch_read_u32(ch_byte_reader* r);
const char* ch_read_str(ch_byte_reader* r);
