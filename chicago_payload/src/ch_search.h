#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <minwindef.h>

#include <stdbool.h>

#include "ch_payload_comm_shared.h"
#include "SDK/datamap.h"

#if 0
#define CH_PL_PRINTF(...) printf("[ch_payload] " __VA_ARGS__);
#else
#define CH_PL_PRINTF(...) \
    while (0) {}
#endif

enum ch_mod_section {
    CH_SEC_TEXT,
    CH_SEC_DATA,
    CH_SEC_RDATA,
    CH_SEC_COUNT,
};

static const char* const ch_mod_sec_names[CH_SEC_COUNT] = {
    ".text",
    ".data",
    ".rdata",
};

typedef struct ch_mod_info {
    struct {
        void* start;
        size_t len;
    } sections[CH_SEC_COUNT];
    // base address of module
    void* base;
    // address of __cinit function
    void* cinit;
    // address of _atexit function
    void* atexit;
    // a filtered & sorted array of static initializers
    void** static_inits;
    size_t n_static_inits;
} ch_mod_info;

// fill the array, on fail send an error and return false
bool ch_get_module_info(ch_mod_info infos[CH_MOD_COUNT]);

// try to find the datamap in the given module, return NULL on fail
datamap_t* ch_find_datamap_by_name(const ch_mod_info* module_info, const char* name);

const void* ch_memmem(const void* restrict haystack,
                      size_t haystack_len,
                      const void* restrict needle,
                      size_t needle_len);
