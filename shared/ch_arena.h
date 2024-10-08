#pragma once

#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

typedef struct ch_arena_chunk {
    struct ch_arena_chunk* prev;
} ch_arena_chunk;

typedef struct ch_arena {
    ch_arena_chunk first_chunk; // must be first
    ch_arena_chunk* last_chunk;
    char* ptr;
    size_t n_free;
    size_t chunk_size;
#ifndef NDEBUG
    size_t total_alloc;
    size_t total_used;
    size_t n_chunks;
#endif
} ch_arena;

#define CH_ARENA_ALIGNMENT sizeof(void*)
#define CH_ALIGN_TO(x, n) (((x) + (n)-1) & ~((n)-1))
#define CH_ARENA_ALIGN(x) CH_ALIGN_TO(x, CH_ARENA_ALIGNMENT)

static ch_arena* ch_arena_new(size_t init_chunk_size)
{
    init_chunk_size = CH_ARENA_ALIGN(max(CH_ARENA_ALIGNMENT, init_chunk_size));
    size_t first_alloc_size = sizeof(ch_arena) + init_chunk_size;
    // the arena lives in the arena, crazy!
    ch_arena* a = malloc(first_alloc_size);
    if (!a)
        return NULL;
    a->first_chunk.prev = NULL;
    a->last_chunk = &a->first_chunk;
    a->ptr = (char*)(a + 1);
    a->n_free = init_chunk_size;
    a->chunk_size = init_chunk_size;
#ifndef NDEBUG
    a->total_alloc = first_alloc_size;
    a->total_used = sizeof(ch_arena);
    a->n_chunks = 1;
#endif
    return a;
}

static void ch_arena_free(ch_arena* arena)
{
    if (!arena)
        return;
    for (ch_arena_chunk* chunk = arena->last_chunk; chunk;) {
        ch_arena_chunk* del_chunk = chunk;
        chunk = chunk->prev;
        free(del_chunk);
    }
}

static void* ch_arena_alloc(ch_arena* arena, size_t n)
{
    assert(arena);
    n = max(n, 1); // make sure size of 0 doesn't return null
    if (arena->n_free < n) {
        arena->chunk_size = arena->chunk_size * 2;
        size_t min_alloc_size = CH_ARENA_ALIGN(n + sizeof(ch_arena_chunk));
        while (arena->chunk_size < min_alloc_size)
            arena->chunk_size *= 2;
        // allocate new chunk
        ch_arena_chunk* new_chunk = malloc(arena->chunk_size);
        if (!new_chunk)
            return NULL;
        new_chunk->prev = arena->last_chunk;
        arena->last_chunk = new_chunk;
        arena->ptr = (char*)(new_chunk + 1);
        arena->n_free = arena->chunk_size - sizeof(ch_arena_chunk);
#ifndef NDEBUG
        arena->n_chunks++;
        arena->total_alloc += arena->chunk_size;
#endif
    }
    void* ret = (void*)arena->ptr;
    n = CH_ARENA_ALIGN(n);
    arena->ptr += n;
    arena->n_free -= n;
#ifndef NDEBUG
    arena->total_used += n;
#endif
    return ret;
}

static void* ch_arena_calloc(ch_arena* arena, size_t n)
{
    void* p = ch_arena_alloc(arena, n);
    if (p)
        memset(p, 0, n);
    return p;
}
