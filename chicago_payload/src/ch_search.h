#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <minwindef.h>

#include <stdbool.h>

#include "ch_payload_comm_shared.h"
#include "SDK/datamap.h"

#if 0
#define CH_PL_PRINTF(...) printf("[ch_payload] " __VA_ARGS__)
#else
#define CH_PL_PRINTF(...) \
    do {                  \
    } while (0)
#endif

struct ch_send_ctx;

enum ch_mod_section_type {
    CH_SEC_TEXT,
    CH_SEC_DATA,
    CH_SEC_RDATA,
    CH_SEC_COUNT,
};

typedef struct ch_mod_sec {
    void* start;
    size_t len;
} ch_mod_sec;

typedef struct ch_mod_info {
    ch_mod_sec sections[CH_SEC_COUNT];
    // base address of module
    void* base;
    // a filtered & sorted array of static initializers
    void** static_inits;
    size_t n_static_inits;
} ch_mod_info;

// TODO - do we really need a mod info for all modules, it might be just server lolol
typedef struct ch_search_ctx {
    ch_mod_info mods[CH_MOD_COUNT];
} ch_search_ctx;

static const char* const ch_mod_sec_names[CH_SEC_COUNT] = {
    ".text",
    ".data",
    ".rdata",
};

typedef struct ch_pattern {
    unsigned char* wildmask;
    unsigned char* bytes;
    size_t len;
} ch_pattern;

// create pattern from string, scratch must have enough space for all data
void ch_parse_pattern_str(const char* str, ch_pattern* out, unsigned char* scratch);

bool ch_pattern_match(const unsigned char* mem, ch_pattern pattern);

// fill the search content, on fail send an error
void ch_get_module_info(struct ch_send_ctx* ctx, ch_search_ctx* sc);

// try to find the datamap in the given module, return NULL on fail
datamap_t* ch_find_datamap_by_name(const struct ch_mod_info* module_info, const char* name);

const void* ch_memmem(const void* restrict haystack,
                      size_t haystack_len,
                      const void* restrict needle,
                      size_t needle_len);

// returns (void*)(-1) if there were duplicates
static const void* ch_memmem_unique(const void* restrict haystack,
                                    size_t haystack_len,
                                    const void* restrict needle,
                                    size_t needle_len)
{
    const void* p = ch_memmem(haystack, haystack_len, needle, needle_len);
    if (!p)
        return p;
    const void* p2 = ch_memmem((char*)p + 1, haystack_len - ((size_t)p - (size_t)haystack) - 1, needle, needle_len);
    if (p2)
        return (void*)(-1);
    return p;
}
