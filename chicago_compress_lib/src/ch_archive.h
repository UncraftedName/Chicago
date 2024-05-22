#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "thirdparty/miniz/ch_miniz.h"
#include "ch_save.h"

#define CH_MAX_GAME_NAME_SIZE 32

// TODO move to shared file
typedef struct ch_byte_array {
    char* arr;
    size_t len;
} ch_byte_array;

typedef struct ch_game_info {
    char name[CH_MAX_GAME_NAME_SIZE];
    char version[CH_MAX_GAME_NAME_SIZE];
} ch_game_info;

bool is_game_name_valid(const char* name, size_t len);

bool ch_game_info_from_file_name(const char* file_name, size_t len, ch_game_info* info);

typedef enum ch_archive_result {
    CH_ARCH_OK,
    CH_ARCH_OOM,
    CH_ARCH_OPEN_FAIL,
    CH_ARCH_READ_FAIL,
    CH_ARCH_BROTLI_FAILED,
    CH_ARCH_FILE_TOO_BIG,
    CH_ARCH_INVALID_COLLECTION,
    CH_ARCH_INVALID_COLLECTION_VERSION,
} ch_archive_result;

#define CH_COLLECTION_FILE_MAX_SIZE (1024 * 1024 * 32)

// returns a fail reason or NULL on success
const char* ch_brotli_decompress(ch_byte_array in, ch_byte_array* out);

/*
* 1) Loads example.zip.br
* 2) Decompresses it
* 3) Adds the byte array to the .zip archive (TODO would like to set the name and stuff of the new file)
* 4) Recompresses it
* 5) Writes to example.zip.br.tmp
* 6) Rename example.zip.br.tmp -> example.zip.br
*/
void ch_add_to_ch_archive(const char* file_name, ch_byte_array extra);

/*
* Call after loading a ch_datamap_collection into memory. This checks the file structure
* and changes all of the relative file offsets into pointers that will point to somewhere
* within the loaded file. Only the datamap, typedescs, & string pointers are set. If
* this function returns OK, then the array can be casted to a ch_datamap_collection*.
*/
ch_archive_result ch_verify_and_fixup_collection_pointers(ch_byte_array collection,
                                                          ch_datamap_collection_header** header_out);

ch_archive_result ch_create_collection_lookup(const ch_datamap_collection_header* header, ch_datamap_collection* out);

ch_archive_result ch_load_file(const char* file_path, ch_byte_array* ba, size_t max_allowed_size);

static inline void ch_free_array(ch_byte_array* ba)
{
    free(ba->arr);
}
