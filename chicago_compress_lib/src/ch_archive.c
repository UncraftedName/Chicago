#include "ch_archive.h"
#include "thirdparty/brotli/include/brotli/decode.h"

ch_archive_result ch_load_file(const char* file_path, ch_byte_array* ba, size_t max_allowed_size)
{
    memset(ba, 0, sizeof *ba);
    FILE* f = fopen(file_path, "rb");
    if (!f)
        return CH_ARCH_OPEN_FAIL;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return CH_ARCH_READ_FAIL;
    }
    long int size = ftell(f);
    if (size == -1L) {
        fclose(f);
        return CH_ARCH_READ_FAIL;
    };
    if ((size_t)size > max_allowed_size) {
        fclose(f);
        return CH_ARCH_FILE_TOO_BIG;
    };
    ba->len = (size_t)size;
    rewind(f);
    ba->arr = malloc(size);
    if (!ba->arr)
        return CH_ARCH_OOM;
    if (fread(ba->arr, 1, size, f) != (size_t)size) {
        free(ba->arr);
        fclose(f);
        return CH_ARCH_READ_FAIL;
    }
    fclose(f);
    return CH_ARCH_OK;
}

const char* ch_brotli_decompress(ch_byte_array in, ch_byte_array* out)
{
    memset(out, 0, sizeof *out);
    BrotliDecoderState* dec_state = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    if (!dec_state)
        return "out of memory";

    size_t bytes_rem_in = in.len;
    const uint8_t* read_bytes_cursor = (const uint8_t*)in.arr;
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
    BrotliDecoderDestroyInstance(dec_state);
    if (out_err) {
        free(out->arr);
    } else {
        // resize the allocation to be only the size required
        char* new_arr = realloc(out->arr, out->len);
        if (new_arr)
            out->arr = new_arr;
    }
    return out_err;
}

ch_archive_result ch_verify_and_fixup_collection_pointers(ch_byte_array bytes)
{
    if (bytes.len < sizeof(ch_datamap_collection) + sizeof(ch_datamap_collection_tag))
        return CH_ARCH_INVALID_COLLECTION;

    ch_datamap_collection* col = (ch_datamap_collection*)bytes.arr;

    ch_datamap_collection_tag* tag =
        (ch_datamap_collection_tag*)(bytes.arr + bytes.len - sizeof(ch_datamap_collection_tag));

    if (strncmp(tag->magic, CH_COLLECTION_MAGIC, sizeof tag->magic))
        return CH_ARCH_INVALID_COLLECTION;
    if (tag->version != CH_DATAMAP_STRUCT_VERSION || col->n_maps == 0)
        return CH_ARCH_INVALID_COLLECTION_VERSION;

#define CH_CHECK_PTR(p)                                                    \
    if ((char*)(p) <= (char*)bytes.arr || (char*)(p) >= bytes.arr + bytes.len) \
    return CH_ARCH_INVALID_COLLECTION

    ch_datamap* dms = (ch_datamap*)(bytes.arr + tag->datamaps_start);
    ch_type_description* tds = (ch_type_description*)(bytes.arr + tag->typedescs_start);
    const char* strings = bytes.arr + tag->strings_start;

    col->maps = dms;
    CH_CHECK_PTR(dms);
    CH_CHECK_PTR(tds);
    CH_CHECK_PTR(strings);

    // the offsets for the various fields are relative to one of the above pointers and they should all lie inside the array
#define CH_FIXUP(packed, rel_to)                   \
    do {                                           \
        if (packed##_rel_off == CH_REL_OFF_NULL) { \
            packed = NULL;                         \
        } else {                                   \
            packed = rel_to + packed##_rel_off;    \
            CH_CHECK_PTR(packed);                  \
        }                                          \
    } while (0)

    for (size_t i = 0; i < col->n_maps; i++) {
        ch_datamap* dm = &dms[i];
        CH_FIXUP(dm->base_map, dms);
        CH_FIXUP(dm->class_name, strings);
        CH_FIXUP(dm->module_name, strings);
        CH_FIXUP(dm->fields, tds);
        if (!!dm->fields ^ !!dm->n_fields)
            return CH_ARCH_INVALID_COLLECTION;
        for (size_t j = 0; j < dm->n_fields; j++) {
            ch_type_description* td = (ch_type_description*)&dm->fields[j]; // const cast
            CH_FIXUP(td->name, strings);
            CH_FIXUP(td->external_name, strings);
            CH_FIXUP(td->embedded_map, dms);
        }
    }

#undef CH_FIXUP
#undef CH_CHECK_PTR

    return CH_ARCH_OK;
}
