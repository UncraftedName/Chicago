#pragma once

#include <stdio.h>
#include <stdlib.h>

typedef struct ch_byte_array {
    char* arr;
    size_t len;
} ch_byte_array;

typedef enum ch_load_file_result {
    CH_LF_OK,
    CH_LF_OOM,
    CH_LF_FAILED_TO_OPEN,
    CH_LF_BROTLI_FAILED,
} ch_load_file_result;

static ch_load_file_result ch_load_file(const char* file_path, ch_byte_array* ba)
{
    memset(ba, 0, sizeof *ba);
    FILE* f = fopen(file_path, "rb");
    if (!f)
        return CH_LF_FAILED_TO_OPEN;
    if (fseek(f, 0, SEEK_END))
        return CH_LF_FAILED_TO_OPEN;
    long int size = ftell(f);
    if (size == -1L)
        return CH_LF_FAILED_TO_OPEN;
    ba->len = (size_t)size;
    rewind(f);
    ba->arr = malloc(size);
    if (!ba->arr)
        return CH_LF_OOM;
    if (fread(ba->arr, 1, size, f) != (size_t)size) {
        free(ba->arr);
        fclose(f);
        return CH_LF_FAILED_TO_OPEN;
    }
    fclose(f);
    return CH_LF_OK;
}

static inline void ch_free_array(ch_byte_array* ba)
{
    free(ba->arr);
}
