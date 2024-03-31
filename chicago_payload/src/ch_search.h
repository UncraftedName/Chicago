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

typedef const unsigned char* ch_ptr;
#define CH_PTR_DIFF(p1, p2) ((size_t)(p1) - (size_t)(p2))

struct ch_send_ctx;

enum ch_mod_section_type {
    CH_SEC_TEXT,
    CH_SEC_DATA,
    CH_SEC_RDATA,
    CH_SEC_COUNT,
};

typedef struct ch_mod_sec {
    ch_ptr start;
    size_t len;
} ch_mod_sec;

typedef struct ch_mod_info {
    ch_mod_sec sections[CH_SEC_COUNT];
    // base address of module
    ch_ptr base;
    // a filtered & sorted array of static initializers
    ch_ptr* static_inits;
    size_t n_static_inits;
} ch_mod_info;

// TODO - do we really need a mod info for all modules, it might be just server lolol
typedef struct ch_search_ctx {
    ch_mod_info mods[CH_MOD_COUNT];
    ch_ptr dump_entity_factories_ctor;
    ch_ptr dump_entity_factories_impl;
} ch_search_ctx;

static inline bool ch_ptr_in_sec(ch_ptr ptr, ch_mod_sec sec)
{
    return ptr >= sec.start && ptr < sec.start + sec.len;
}

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

bool ch_pattern_match(ch_ptr mem, ch_pattern pattern);

// fill the search content, on fail send an error
void ch_get_module_info(struct ch_send_ctx* ctx, ch_search_ctx* sc);

void ch_find_entity_factory_cvar(struct ch_send_ctx* ctx, ch_search_ctx* sc);

void ch_find_static_inits_from_single(struct ch_send_ctx* ctx, ch_mod_info* mod, ch_ptr static_init_func);

ch_ptr ch_memmem(ch_ptr haystack, size_t haystack_len, ch_ptr needle, size_t needle_len);

// calls the callback on each match which returns false if we should continue searching
ch_ptr ch_memmem_cb(ch_ptr haystack,
                    size_t haystack_len,
                    ch_ptr needle,
                    size_t needle_len,
                    bool (*cb)(ch_ptr match, void* user_data),
                    void* user_data);

#define CH_MEM_DUP ((ch_ptr)(-1))

// returns CH_MEM_DUP if there were duplicates
static ch_ptr ch_memmem_unique(ch_ptr haystack, size_t haystack_len, ch_ptr needle, size_t needle_len)
{
    ch_ptr p = ch_memmem(haystack, haystack_len, needle, needle_len);
    if (!p)
        return p;
    ch_ptr p2 = ch_memmem(p + 1, haystack_len - CH_PTR_DIFF(p, haystack) - 1, needle, needle_len);
    if (p2)
        return CH_MEM_DUP;
    return p;
}
