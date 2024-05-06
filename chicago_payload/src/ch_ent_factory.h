#pragma once

#include <stdint.h>

#include "ch_search.h"

// mostly yoinked from sst
// https://git.mikes.software/sst/tree/src/dictmaptree.h

typedef struct ch_ent_factory {
    struct {
        void*(__fastcall* create)(void* thisptr, int _edx, const char* classname);
        // there are more entries here but we don't need them :)
    }* vt;
} ch_ent_factory;

typedef struct ch_ent_factory_dict {
    const ch_ptr* vt;
    bool (*cmp)(const char** k1, const char** k2);
    struct {
        const void* mem;
        int32_t alloc_count, grow_size;
    } utl_mem;
    uint16_t root, count, first_free, last_alloc;
    struct ch_ent_dict_node {
        struct {
            uint16_t l, r, p, color;
        } links;
        const char* k;
        const ch_ent_factory* v;
    }* nodes;
} ch_ent_factory_dict;

#define CH_FACTORY_DICT_INVALID_IDX ((uint16_t)-1)

const ch_ent_factory_dict* ch_find_ent_factory_dict(struct ch_send_ctx* ctx, const ch_mod_info* mod_server);
void ch_send_ent_factory_kv(struct ch_send_ctx* ctx, const struct ch_search_ctx* sc, const ch_ent_factory_dict* factory);
