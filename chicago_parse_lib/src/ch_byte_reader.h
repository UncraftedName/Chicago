#pragma once

#include <memory.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef struct ch_byte_reader {
    const unsigned char *cur, *end;
} ch_byte_reader;

inline bool ch_br_overflowed(const ch_byte_reader* br)
{
    return br->cur > br->end;
}

inline void ch_br_set_overflowed(ch_byte_reader* br)
{
    br->end = br->cur - 1;
}

inline bool ch_br_could_skip(const ch_byte_reader* br, size_t n)
{
    return !ch_br_overflowed(br) && br->cur + n <= br->end;
}

inline bool ch_br_skip(ch_byte_reader* br, size_t n)
{
    if (ch_br_could_skip(br, n))
        br->cur += n;
    else
        ch_br_set_overflowed(br);
    return ch_br_overflowed(br);
}

inline void ch_br_skip_unchecked(ch_byte_reader* br, size_t n)
{
    br->cur += n;
}

inline size_t ch_br_remaining(const ch_byte_reader* br)
{
    return ch_br_overflowed(br) ? 0 : (size_t)br->end - (size_t)br->cur;
}

inline void ch_br_skip_capped(ch_byte_reader* br, size_t n)
{
    ch_br_skip_unchecked(br, min(n, ch_br_remaining(br)));
}

inline size_t ch_br_strnlen(const ch_byte_reader* br, size_t max_search)
{
    max_search = min(max_search, ch_br_remaining(br));
    return strnlen((const char*)br->cur, max_search);
}

inline size_t ch_br_strlen(const ch_byte_reader* br)
{
    return strnlen((const char*)br->cur, ch_br_remaining(br));
}

inline bool ch_br_read(ch_byte_reader* br, void* dest, size_t n)
{
    if (ch_br_could_skip(br, n)) {
        memcpy(dest, br->cur, n);
        br->cur += n;
    } else {
        ch_br_set_overflowed(br);
    }
    return !ch_br_overflowed(br);
}

/*
* 1) creates and returns a new reader that has a size of chunk_size starting at br->cur
* 2) br->cur is adjusted to jump over the current chunk
* 
* Use case:
* 
* br_chunk = ch_br_split_skip(&br, 666);
* ch_parsed_this_crazy_next_chunk(br_chunk);
*/
inline ch_byte_reader ch_br_split_skip(ch_byte_reader* br, size_t chunk_size)
{
    if (ch_br_could_skip(br, chunk_size)) {
        ch_byte_reader ret = {.cur = br->cur, .end = br->cur + chunk_size};
        br->cur += chunk_size;
        return ret;
    } else {
        ch_byte_reader ret = {.cur = br->cur, .end = br->end};
        ch_br_set_overflowed(br);
        return ret;
    }
}

/*
* Same as above, but also swaps the two readers. Use case:
* 
* br_after_chunk = ch_br_split_skip_swap(&ctx->br, 666);
* ch_parse_this_crazy_next_chunk_with_context(&ctx);
* ctx->br = br_after_chunk
*/
inline ch_byte_reader ch_br_split_skip_swap(ch_byte_reader* br, size_t chunk_size)
{
    if (ch_br_could_skip(br, chunk_size)) {
        ch_byte_reader ret = {.cur = br->cur + chunk_size, .end = br->end};
        br->end = br->cur + chunk_size;
        return ret;
    } else {
        ch_byte_reader ret = {.cur = br->end, .end = br->end};
        ch_br_set_overflowed(&ret);
        return ret;
    }
}

/*
* Creates & returns a byte reader at some position relative
* to the given reader. The given reader is unchanged.
*/
inline ch_byte_reader ch_br_jmp_rel(const ch_byte_reader* br, size_t jmp_size)
{
    if (ch_br_could_skip(br, jmp_size)) {
        ch_byte_reader ret = {.cur = br->cur + jmp_size, .end = br->end};
        return ret;
    } else {
        ch_byte_reader ret = {.cur = br->end, .end = br->end};
        ch_br_set_overflowed(&ret);
        return ret;
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
            ch_br_set_overflowed(br);                    \
            return (ret_type)0;                          \
        }                                                \
    }

CH_BR_DEFINE_PRIMITIVE_READ(ch_br_read_16, int16_t)
CH_BR_DEFINE_PRIMITIVE_READ(ch_br_read_32, int32_t)
CH_BR_DEFINE_PRIMITIVE_READ(ch_br_read_u32, uint32_t)
