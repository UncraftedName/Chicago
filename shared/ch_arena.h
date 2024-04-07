#pragma once

#include "stdlib.h"
#include "string.h"

typedef struct ch_arena_chunk {
    struct ch_arena_chunk* next;
} ch_arena_chunk;

typedef struct ch_arena {
    ch_arena_chunk* chunk;
    uintptr_t ptr;
    size_t n_free;
    size_t chunk_size;
} ch_arena;

#define CH_ARENA_ALIGNMENT (sizeof(void*))
#define CH_ALIGN(x) (((x) + CH_ARENA_ALIGNMENT - 1) & ~(CH_ARENA_ALIGNMENT - 1))

static inline void ch_arena_init(ch_arena* arena, size_t init_chunk_size)
{
    memset(arena, 0, sizeof *arena);
    arena->chunk_size = CH_ALIGN(max(1, init_chunk_size));
}

static inline void ch_arena_destroy(ch_arena* arena)
{
    while (arena->chunk) {
        ch_arena_chunk* cur = arena->chunk;
        arena->chunk = arena->chunk->next;
        free(cur);
    }
}

static inline void* ch_arena_alloc(ch_arena* arena, size_t n)
{
    if (arena->n_free < n || !arena->chunk) {
        n = CH_ALIGN(n + sizeof(ch_arena_chunk));
        while (arena->chunk_size < n)
            arena->chunk_size *= 2;
        char* p = malloc(arena->chunk_size);
        if (!p)
            return NULL;
        ch_arena_chunk* prev_head = arena->chunk;
        arena->chunk = p;
        arena->chunk->next = prev_head;
        arena->ptr = arena->chunk + sizeof(ch_arena_chunk);
        arena->n_free = arena->chunk_size - sizeof(ch_arena_chunk);
    }
    void* ret = (void*)arena->ptr;
    n = CH_ALIGN(n);
    arena->ptr += n;
    arena->n_free -= n;
    return ret;
}

static inline void* ch_arena_calloc(ch_arena* arena, size_t n)
{
    void* p = ch_arena_alloc(arena, n);
    if (p)
        memset(p, 0, n);
    return p;
}
