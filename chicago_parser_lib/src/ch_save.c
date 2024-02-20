#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ch_save_internal.h"

ch_err ch_parse_save_from_file(ch_parsed_save_data* parsed_data, const char* file_path)
{
    FILE* f = fopen(file_path, "rb");
    if (!f)
        return CH_ERR_FAILED_TO_READ_FILE;
    if (fseek(f, 0, SEEK_END))
        return CH_ERR_FAILED_TO_READ_FILE;
    long int size = ftell(f);
    if (size == -1L)
        return CH_ERR_FAILED_TO_READ_FILE;
    rewind(f);
    void* bytes = malloc(size);
    if (!bytes)
        return CH_ERR_OEM;
    if (fread(bytes, 1, size, f) != size) {
        free(bytes);
        return CH_ERR_FAILED_TO_READ_FILE;
    }
    ch_err ret = ch_parse_save_from_bytes(parsed_data, bytes, size);
    free(bytes);
    return ret;
}

ch_err ch_parse_save_from_bytes(ch_parsed_save_data* parsed_data, void* bytes, size_t n_bytes)
{
    ch_byte_reader r = {
        .cur = bytes,
        .end = (unsigned char*)bytes + n_bytes,
        .overflowed = false,
    };
    return ch_parse_save_from_reader(parsed_data, &r);
}

ch_err ch_parse_save_from_reader(ch_parsed_save_data* parsed_data, ch_byte_reader* r)
{
    strncpy(parsed_data->header.id, "test", 4);
    return CH_ERR_NONE;
}

void ch_free_save_data(ch_parsed_save_data* parsed_data)
{
    for (ch_state_file *sf_p = parsed_data->state_files, *sf_n = sf_p ? sf_p->next : NULL; sf_p; sf_p = sf_n)
        free(sf_p);
    memset(parsed_data, 0, sizeof *parsed_data);
}
