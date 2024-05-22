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

// the offsets for the various fields are relative to one of the pointers from the header and they should all lie inside the array

#define CH_CHECK_PTR(p)                                                        \
    if ((char*)(p) <= (char*)bytes.arr || (char*)(p) >= bytes.arr + bytes.len) \
    return CH_ARCH_INVALID_COLLECTION

#define CH_FIXUP_TYPED(packed, rel_to, type)            \
    do {                                                \
        if (packed##_rel_off == CH_REL_OFF_NULL) {      \
            packed = NULL;                              \
        } else {                                        \
            packed = (type)(rel_to + packed##_rel_off); \
            CH_CHECK_PTR(packed);                       \
        }                                               \
    } while (0)

#define CH_FIXUP(packed, rel_to) CH_FIXUP_TYPED(packed, rel_to, void*)

ch_archive_result ch_verify_and_fixup_collection_pointers(ch_byte_array bytes,
                                                          ch_datamap_collection_header** header_out)
{
    if (bytes.len < sizeof(ch_datamap_collection_header))
        return CH_ARCH_INVALID_COLLECTION;

    ch_datamap_collection_header* hd = (ch_datamap_collection_header*)bytes.arr;
    *header_out = hd;

    if (strncmp(hd->magic, CH_COLLECTION_FILE_MAGIC, sizeof hd->magic))
        return CH_ARCH_INVALID_COLLECTION;
    if (hd->version != CH_DATAMAP_STRUCT_VERSION || hd->n_datamaps == 0)
        return CH_ARCH_INVALID_COLLECTION_VERSION;

    CH_FIXUP_TYPED(hd->dms, bytes.arr, ch_datamap*);
    CH_FIXUP_TYPED(hd->tds, bytes.arr, ch_type_description*);
    CH_FIXUP_TYPED(hd->lnks, bytes.arr, ch_linked_name*);
    CH_FIXUP_TYPED(hd->strs, bytes.arr, const char*);
    CH_CHECK_PTR((char*)(hd->dms + hd->n_datamaps) - 1);
    CH_CHECK_PTR((char*)(hd->lnks + hd->n_linked_names) - 1);

    // fixup datamaps & type descs
    for (size_t i = 0; i < hd->n_datamaps; i++) {
        ch_datamap* dm = (ch_datamap*)&hd->dms[i]; // cast away const
        CH_FIXUP(dm->base_map, hd->dms);
        CH_FIXUP(dm->class_name, hd->strs);
        CH_FIXUP(dm->module_name, hd->strs);
        CH_FIXUP(dm->fields, hd->tds);
        if (!!dm->fields ^ !!dm->n_fields)
            return CH_ARCH_INVALID_COLLECTION;
        for (size_t j = 0; j < dm->n_fields; j++) {
            ch_type_description* td = (ch_type_description*)&dm->fields[j];
            CH_FIXUP(td->name, hd->strs);
            CH_FIXUP(td->external_name, hd->strs);
            CH_FIXUP(td->embedded_map, hd->dms);
            if (td->ch_offset >= dm->ch_size)
                return CH_ARCH_INVALID_COLLECTION;
        }
    }

    for (size_t i = 0; i < hd->n_linked_names; i++) {
        ch_linked_name* ln = (ch_linked_name*)&hd->lnks[i];
        CH_FIXUP(ln->linked_name, hd->strs);
        CH_FIXUP(ln->dm, hd->dms);
    }

    return CH_ARCH_OK;
}

ch_archive_result ch_create_collection_lookup(const ch_datamap_collection_header* header, ch_datamap_collection* out)
{
    out->lookup = hashmap_new(sizeof(ch_datamap_lookup_entry),
                              header->n_datamaps + header->n_linked_names,
                              0,
                              0,
                              ch_datamap_collection_name_hash,
                              ch_datamap_collection_name_compare,
                              NULL,
                              NULL);
    if (!out->lookup)
        return CH_ARCH_OOM;

    for (size_t i = 0; i < header->n_datamaps; i++) {
        ch_datamap_lookup_entry entry = {.name = header->dms[i].class_name, .datamap = &header->dms[i]};
        const void* v = hashmap_set(out->lookup, &entry);
        assert(!v);
        if (hashmap_oom(out->lookup))
            return CH_ARCH_OOM;
    }
    for (size_t i = 0; i < header->n_linked_names; i++) {
        ch_datamap_lookup_entry entry = {.name = header->lnks[i].linked_name, .datamap = header->lnks[i].dm};
        const void* v = hashmap_set(out->lookup, &entry);
        assert(!v);
        if (hashmap_oom(out->lookup))
            return CH_ARCH_OOM;
    }

    return CH_ARCH_OK;
}
