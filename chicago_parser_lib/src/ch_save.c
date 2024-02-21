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

    ch_save_header* sh = &ctx->data->header;
    ch_br_read(&ctx->br, sh, sizeof *sh);

    if (ctx->br.overflowed)
        return CH_ERR_READER_OVERFLOWED;

    if (strncmp(sh->id, "JSAV", 4) || sh->version != 0x73 || sh->symbol_count < 0 || sh->symbol_table_size_bytes < 0 ||
        ctx->br.cur + sh->symbol_table_size_bytes + sh->header_fields_size_bytes > ctx->br.end)
        return CH_ERR_INVALID_HEADER;

    ch_err ret = CH_ERR_NONE;

    if (sh->symbol_table_size_bytes > 0) {
        const char* tmp_symbol_ptr = ctx->symbols = ctx->br.cur;
        const char* tmp_symbols_end = tmp_symbol_ptr + sh->symbol_table_size_bytes;
        ctx->n_symbols = sh->symbol_count;
        ctx->symbol_offs = calloc(ctx->n_symbols, sizeof *ctx->symbol_offs);
        if (!ctx->symbol_offs)
            return CH_ERR_OUT_OF_MEMORY;

        for (ch_symbol_offset i = 0; i < ctx->n_symbols; i++) {
            if (*tmp_symbol_ptr)
                ctx->symbol_offs[i] = tmp_symbol_ptr - ctx->symbols;
            tmp_symbol_ptr += strnlen(tmp_symbol_ptr, tmp_symbols_end - tmp_symbol_ptr) + 1;
            if (tmp_symbol_ptr > tmp_symbols_end) {
                ret = CH_ERR_INVALID_SYMBOL_TABLE;
                break;
            }
        }
        ch_br_skip(&ctx->br, sh->symbol_table_size_bytes);
    }

    // TODO : READ THE FIELDS HERE

    if (ret == CH_ERR_NONE) {

        ch_br_skip(&ctx->br, sh->header_fields_size_bytes);
        int n_state_files = ch_br_read_32(&ctx->br);
        if (n_state_files < 0)
            ret = CC_ERR_INVALID_NUMBER_OF_STATE_FILES;

        ch_state_file** sf = &ctx->data->state_files;
        for (int i = 0; i < n_state_files && !ctx->br.overflowed && ret == CH_ERR_NONE; i++) {
            *sf = calloc(1, sizeof **sf);
            ch_br_read(&ctx->br, (**sf).name, sizeof((**sf).name));
            int sf_len_bytes = ch_br_read_32(&ctx->br);
            if (sf_len_bytes < 0) {
                ret = CH_ERR_INVALID_STATE_FILE_LENGTH;
                break;
            }
            if (ctx->br.overflowed)
                break;
            ch_byte_reader tmp_reader = ctx->br;
            ctx->br.end = ctx->br.cur + sf_len_bytes;
            ret = ch_parse_state_file(ctx, *sf);
            ctx->br = tmp_reader;
            ch_br_skip(&ctx->br, sf_len_bytes);
            sf = &(**sf).next;
        }
        if (ret == CH_ERR_NONE && ctx->br.overflowed)
            ret = CH_ERR_READER_OVERFLOWED;
    }

    free(ctx->symbol_offs);
    return ret;
}

ch_err ch_parse_state_file(ch_parsed_save_ctx* ctx, ch_state_file* sf)
{
    ch_br_read(&ctx->br, sf->id, sizeof sf->id); // no need to verify
    return CH_ERR_NONE;
}

void ch_free_save_data(ch_parsed_save_data* parsed_data)
{
    while (parsed_data->state_files) {
        ch_state_file* sf = parsed_data->state_files;
        parsed_data->state_files = parsed_data->state_files->next;
        free(sf);
    }
    memset(parsed_data, 0, sizeof *parsed_data);
}
