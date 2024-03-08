#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDK/built_in_descs.h"
#include "ch_field_reader.h"
#include "ch_save_internal.h"

ch_err ch_parse_save_path(ch_parsed_save_data* parsed_data, const char* file_path)
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
        fclose(f);
        return CH_ERR_FAILED_TO_READ_FILE;
    }
    fclose(f);
    ch_err ret = ch_parse_save_bytes(parsed_data, bytes, size);
    free(bytes);
    return ret;
}

ch_err ch_parse_save_bytes(ch_parsed_save_data* parsed_data, void* bytes, size_t n_bytes)
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
        return CH_ERR_SAV_BAD_TAG;

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

    ch_byte_reader br_after_fields = ch_br_split_skip_swap(br, global_fields_size_bytes);
    if (br_after_fields.overflowed) {
        err = CH_ERR_READER_OVERFLOWED;
    } else {
        // TODO : READ THE GLOBAL FIELDS HERE
        ch_parsed_fields parsed_fields;
        err = ch_br_read_save_fields(ctx, "GameHeader", &game_header_map, &parsed_fields);
    }
    *br = br_after_fields;
    ch_free_symbol_table(&ctx->st);
    if (err)
        return err;

    // read state files

    // TODO put these in a single array
    int n_state_files = ch_br_read_32(br);
    if (br->overflowed)
        return CH_ERR_READER_OVERFLOWED;
    if (n_state_files < 0)
        return CC_ERR_BAD_STATE_FILE_COUNT;

    ch_state_file** sf = &ctx->data->state_files;
    for (int i = 0; i < n_state_files && !err; i++) {
        *sf = calloc(1, sizeof **sf);
        if (!ch_br_read(br, (**sf).name, sizeof((**sf).name)))
            return CH_ERR_READER_OVERFLOWED;
        int sf_len_bytes = ch_br_read_32(br);
        if (sf_len_bytes < 0)
            return CH_ERR_BAD_STATE_FILE_LENGTH;
        ch_byte_reader br_after_sf = ch_br_split_skip_swap(br, sf_len_bytes);
        if (br_after_sf.overflowed)
            return CH_ERR_READER_OVERFLOWED;
        err = ch_parse_state_file(ctx, *sf);
        *br = br_after_sf;
        sf = &(**sf).next;
    }

    return err;
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
        return CC_ERR_BAD_STATE_FILE_NAME;

    if (!strncmp(sf->name + i, ".hl1", 4)) {
        sf->sf_type = CH_SF_SAVE_DATA;
        return ch_parse_hl1(ctx, &sf->sf_save_data);
    } else if (!strncmp(sf->name + i, ".hl2", 4)) {
        sf->sf_type = CH_SF_ADJACENT_CLIENT_STATE;
        return ch_parse_hl2(ctx, &sf->sf_adjacent_client_state);
    } else if (!strncmp(sf->name + i, ".hl3", 4)) {
        sf->sf_type = CH_SF_ENTITY_PATCH;
        return ch_parse_hl3(ctx, &sf->sf_entity_patch);
    } else {
        return CC_ERR_BAD_STATE_FILE_NAME;
    }
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
