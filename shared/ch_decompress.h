#pragma once

#include "ch_byte_array.h"
#include "thirdparty/brotli/include/brotli/decode.h"

// returns a fail reason or NULL on success
static const char* ch_brotli_decompress(const ch_byte_array* in, ch_byte_array* out)
{
    memset(out, 0, sizeof *out);
    BrotliDecoderState* dec_state = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    if (!dec_state)
        return "out of memory";

    size_t bytes_rem_in = in->len;
    const uint8_t* read_bytes_cursor = (const uint8_t*)in->arr;
    const char* out_err = NULL;

    out->arr = NULL;
    size_t out_alloc_size = 1024 * 1024;

    for (;;) {
        out_alloc_size *= 2;
        char* new_arr = realloc(out->arr, out_alloc_size);
        if (!new_arr) {
            out_err = "out of memory";
            break;
        }
        out->arr = new_arr;
        size_t bytes_rem_out = out_alloc_size - out->len;
        uint8_t* out_cursor = (uint8_t*)out->arr + out->len;
        BrotliDecoderResult res = BrotliDecoderDecompressStream(dec_state,
                                                                &bytes_rem_in,
                                                                &read_bytes_cursor,
                                                                &bytes_rem_out,
                                                                &out_cursor,
                                                                &out->len);
        if (res == BROTLI_DECODER_RESULT_SUCCESS)
            break;
        else if (res != BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            out_err = BrotliDecoderErrorString(BrotliDecoderGetErrorCode(dec_state));
            break;
        }
    }
    if (out_err)
        free(out->arr);
    BrotliDecoderDestroyInstance(dec_state);
    return out_err;
}
