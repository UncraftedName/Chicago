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
#define CH_COLLECTION_FILE_MAGIC "chicago"

/*
* This tag is put at the end of the file so that we can allocate one big bungus buffer
* and assign our ch_datamap_collection pointer to that, then free it once we're done.
* When reading from disk, we can jump to the end of the file and read only the tag
* without issue. But when using miniz, we can only decompress blocks of at least 64KB
* at a time. Since the datamap collection files should be relatively small, I don't
* care about the overhead of copying the entire file into mem before checking if the
* tag is valid.
*/
typedef struct ch_datamap_collection_tag {
    size_t n_datamaps;
    // absolute offsets from file start
    size_t datamaps_start;
    size_t typedescs_start;
    size_t strings_start;
    // keep these guys at the end across all versions
    size_t version; // CH_DATAMAP_STRUCT_VERSION
    char magic[8];  // CH_COLLECTION_FILE_MAGIC
} ch_datamap_collection_tag;

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
ch_archive_result ch_verify_and_fixup_collection_pointers(ch_byte_array collection);

ch_archive_result ch_load_file(const char* file_path, ch_byte_array* ba, size_t max_allowed_size);

static inline void ch_free_array(ch_byte_array* ba)
{
    free(ba->arr);
}
