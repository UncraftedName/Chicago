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
        return CH_ERR_OUT_OF_MEMORY;
    if (fread(bytes, 1, size, f) != (size_t)size) {
        free(bytes);
        return CH_ERR_FAILED_TO_READ_FILE;
    }
    ch_err ret = ch_parse_save_from_bytes(parsed_data, bytes, size);
    free(bytes);
    return ret;
}

ch_err ch_parse_save_from_bytes(ch_parsed_save_data* parsed_data, void* bytes, size_t n_bytes)
{
    memset(parsed_data, 0, sizeof *parsed_data);
    ch_parsed_save_ctx ctx = {
        .data = parsed_data,
        .br =
            {
                .cur = bytes,
                .end = (unsigned char*)bytes + n_bytes,
                .overflowed = false,
            },
    };
    return ch_parse_save_ctx(&ctx);
}

ch_err ch_parse_save_ctx(ch_parsed_save_ctx* ctx)
{
    // TODO write down the exact logic that happens here:
    // the load command technically starts in Host_Loadgame_f
    // then we start parsing in CSaveRestore::LoadGame

    // this first section happens in CSaveRestore::SaveReadHeader

    ch_byte_reader* br = &ctx->br;

    // read tag
    ch_br_read(br, &ctx->data->tag, sizeof ctx->data->tag);
    const ch_tag expected_tag = {.id = {'J', 'S', 'A', 'V'}, .version = 0x73};
    if (memcmp(&ctx->data->tag, &expected_tag, sizeof expected_tag))
        return CH_ERR_HEADER_INVALID_TAG;

    // read misc header info
    int32_t global_fields_size_bytes = ch_br_read_32(br);
    int32_t st_n_symbols = ch_br_read_32(br);
    int32_t st_size_bytes = ch_br_read_32(br);
    if (br->overflowed)
        return CH_ERR_READER_OVERFLOWED;

    ch_err err;

    // read symbol table
    if (st_size_bytes > 0) {
        ch_byte_reader br_st = ch_br_split_skip(br, st_size_bytes);
        if (br->overflowed)
            return CH_ERR_READER_OVERFLOWED;
        err = ch_br_read_symbol_table(&br_st, &ctx->st, st_n_symbols);
        if (err)
            return err;
    }

    // read global fields
    {
        ch_byte_reader br_gf = ch_br_split_skip(br, global_fields_size_bytes);
        if (br->overflowed) {
            err = CH_ERR_READER_OVERFLOWED;
        } else {
            // TODO : READ THE GLOBAL FIELDS HERE
            err = CH_ERR_NONE;
            (void)br_gf;
        }
        ch_free_symbol_table(&ctx->st);
        if (err)
            return err;
    }

    // read state files

    int n_state_files = ch_br_read_32(br);
    if (n_state_files < 0)
        return CC_ERR_INVALID_NUMBER_OF_STATE_FILES;

    ch_state_file** sf = &ctx->data->state_files;
    for (int i = 0; i < n_state_files && !ctx->br.overflowed; i++) {
        *sf = calloc(1, sizeof **sf);
        ch_br_read(br, (**sf).name, sizeof((**sf).name));
        int sf_len_bytes = ch_br_read_32(br);
        if (sf_len_bytes < 0)
            return CH_ERR_INVALID_STATE_FILE_LENGTH;
        if (ctx->br.overflowed)
            return CH_ERR_READER_OVERFLOWED;
        ch_byte_reader br_after_sf = ch_br_split_skip_swap(br, sf_len_bytes);
        err = ch_parse_state_file(ctx, *sf);
        if (err)
            return err;
        *br = br_after_sf;
        sf = &(**sf).next;
    }

    return CH_ERR_NONE;
}

ch_err ch_parse_state_file(ch_parsed_save_ctx* ctx, ch_state_file* sf)
{
    // TODO describe in more detail: CServerGameDLL::LevelInit

    // no memrchr in msvc, sad!
    size_t i = sizeof(sf->name) - 3;
    for (; i > 0; i--)
        if (sf->name[i] == '.')
            break;
    if (i == 0)
        return CC_ERR_INVALID_STATE_FILE_NAME;

    if (!strncmp(sf->name + i, ".hl1", 4)) {
        sf->sf_type = CH_SF_SAVE_DATA;
        return ch_parse_hl1(ctx, &sf->sf_save_data);
    } else if (!strncmp(sf->name + i, ".hl2", 4)) {
        sf->sf_type = CH_SF_ADJACENT_CLIENT_STATE;
        return ch_parse_hl2(ctx, &sf->sf_adjacent_client_state);
    } else if (!strncmp(sf->name + i, ".hl3", 4)) {

    } else {
        return CC_ERR_INVALID_STATE_FILE_NAME;
    }

    return CH_ERR_NONE;
}

void ch_free_parsed_save_data(ch_parsed_save_data* parsed_data)
{
    while (parsed_data->state_files) {
        ch_state_file* sf = parsed_data->state_files;
        parsed_data->state_files = parsed_data->state_files->next;
        // TODO - free sf data here
        free(sf);
    }
    memset(parsed_data, 0, sizeof *parsed_data);
}
