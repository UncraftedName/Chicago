#include <string.h>
#include <inttypes.h>
#include <ctype.h>

#include "ch_field_reader.h"
#include "ch_save_internal.h"

#define CH_INVALID_HANDLE_INDEX 0xFFFFFFFF
#define CH_MAX_EDICT_BITS 11
#define CH_MAX_EDICTS (1 << CH_MAX_EDICT_BITS)
#define CH_NUM_ENT_ENTRY_BITS (CH_MAX_EDICT_BITS + 1)
#define CH_NUM_ENT_ENTRIES (1 << CH_NUM_ENT_ENTRY_BITS)
#define CH_ENT_ENTRY_MASK (CH_NUM_ENT_ENTRIES - 1)
#define CH_NUM_SERIAL_NUM_BITS (32 - CH_NUM_ENT_ENTRY_BITS)

ch_err ch_br_read_symbol_table(ch_byte_reader* br, ch_symbol_table* st, int n_symbols)
{
    if (n_symbols < 0)
        return CH_ERR_BAD_SYMBOL_TABLE;
    st->symbols = (const char*)br->cur;
    st->n_symbols = n_symbols;
    // TODO - change to realloc
    st->symbol_offs = calloc(st->n_symbols, sizeof *st->symbol_offs);
    if (!st->symbol_offs)
        return CH_ERR_OUT_OF_MEMORY;

    const unsigned char* start = br->cur;
    for (int i = 0; i < n_symbols && !br->overflowed; i++) {
        if (*br->cur)
            st->symbol_offs[i] = br->cur - start;
        ch_br_skip(br, strnlen((const char*)br->cur, br->end - br->cur) + 1);
    }
    if (br->overflowed) {
        ch_free_symbol_table(st);
        return CH_ERR_BAD_SYMBOL_TABLE; // being more specific than reader overflow
    } else {
        return CH_ERR_NONE;
    }
}

ch_err ch_br_read_symbol(ch_byte_reader* br, const ch_symbol_table* st, const char** symbol)
{
    short idx = ch_br_read_16(br);
    if (br->overflowed)
        return CH_ERR_READER_OVERFLOWED;
    if (idx < 0 || idx >= st->n_symbols)
        return CH_ERR_BAD_SYMBOL;
    *symbol = st->symbols + st->symbol_offs[idx];
    return CH_ERR_NONE;
}

ch_err ch_br_start_block(const ch_symbol_table* st, ch_byte_reader* br_cur, ch_block* block)
{
    int16_t size_bytes = ch_br_read_16(br_cur);
    if (br_cur->overflowed)
        return CH_ERR_READER_OVERFLOWED;
    if (size_bytes < 0)
        return CH_ERR_BAD_BLOCK_START;
    CH_RET_IF_ERR(ch_br_read_symbol(br_cur, st, &block->symbol));
    block->reader_after_block = ch_br_split_skip_swap(br_cur, size_bytes);
    if (br_cur->overflowed)
        return CH_ERR_BAD_BLOCK_START;
    return CH_ERR_NONE;
}

ch_err ch_br_end_block(ch_byte_reader* br, ch_block* block, bool check_match)
{
    if (check_match && br->cur != block->reader_after_block.cur)
        return CH_ERR_BAD_BLOCK_END;
    *br = block->reader_after_block;
    return CH_ERR_NONE;
}

static ch_err ch_br_restore_simple_field(ch_parsed_save_ctx* ctx, unsigned char* dest, const ch_type_description* td)
{
    ch_field_type ft = td->type;
    ch_byte_reader* br = &ctx->br;

    switch (ft) {
        case FIELD_STRING:
        case FIELD_MODELNAME:
        case FIELD_SOUNDNAME:
        case FIELD_FUNCTION:
        case FIELD_MODELINDEX:
        case FIELD_MATERIALINDEX: {
            for (size_t i = 0; ch_br_remaining(br) > 0 && i < td->n_elems; i++) {
                size_t len = ch_br_strlen(br);
                if (len > 0) {
                    char** alloc_dest = (char**)dest + i;
                    CH_CHECKED_ALLOC(*alloc_dest, ch_arena_alloc(ctx->arena, len + 1));
                    ch_br_read(br, *alloc_dest, len);
                    (*alloc_dest)[len] = '\0';
                }
                ch_br_skip_capped(br, 1);
            }
            break;
        }
        case FIELD_FLOAT:
        case FIELD_VECTOR:
        case FIELD_QUATERNION:
        case FIELD_INTEGER:
        case FIELD_BOOLEAN:
        case FIELD_SHORT:
        case FIELD_CHARACTER:
        case FIELD_COLOR32:
        case FIELD_CLASSPTR:
        case FIELD_EHANDLE:
        case FIELD_EDICT:
        case FIELD_POSITION_VECTOR:
        case FIELD_TIME:
        case FIELD_TICK:
        case FIELD_VMATRIX:
        case FIELD_VMATRIX_WORLDSPACE:
        case FIELD_MATRIX3X4_WORLDSPACE:
        case FIELD_INTERVAL:
        case FIELD_VECTOR2D: {
            size_t bytes_avail = ch_br_remaining(br);
            if (!td->total_size_bytes || bytes_avail < td->total_size_bytes)
                return CH_ERR_BAD_FIELD_READ;
            ch_br_read(br, dest, min(td->total_size_bytes, bytes_avail));
            break;
        }
        case FIELD_INPUT:
        case FIELD_VOID:
        case FIELD_CUSTOM:
        case FIELD_EMBEDDED:
        case FIELD_TYPECOUNT:
        default:
            assert(0);
            return CH_ERR_BAD_FIELD_TYPE;
    }

    // TODO apply field-specific fixups

    return ctx->br.overflowed ? CH_ERR_READER_OVERFLOWED : CH_ERR_NONE;
}

ch_err ch_br_restore_fields(ch_parsed_save_ctx* ctx,
                            const char* expected_symbol,
                            const ch_datamap* dm,
                            unsigned char* class_ptr)
{
    // CRestore::ReadFields

    ch_byte_reader* br = &ctx->br;

    if (ch_br_read_16(br) != 4)
        return CH_ERR_BAD_FIELDS_MARKER;

    const char* symbol;
    CH_RET_IF_ERR(ch_br_read_symbol(br, &ctx->st, &symbol));

    if (_stricmp(symbol, expected_symbol)) {
        CH_PARSER_LOG_ERR(ctx, "error attempting to read symbol %s, expected %s", symbol, expected_symbol);
        return CH_ERR_BAD_SYMBOL;
    }

    // most of the time fields will be stored in the same order as in the datamap
    int cookie = -1;

    int n_fields = ch_br_read_32(br);

    for (int i = 0; i < n_fields; i++) {
        ch_block block;
        CH_RET_IF_ERR(ch_br_start_block(&ctx->st, &ctx->br, &block));

        // CRestore::FindField
        const ch_type_description* field = NULL;
        size_t n_tests = 0;
        for (; n_tests < dm->n_fields; n_tests++) {
            cookie = (cookie + 1) % dm->n_fields;
            field = &dm->fields[cookie];
            if (!_stricmp(field->name, block.symbol))
                break;
        }

        if (n_tests == dm->n_fields) {
            CH_PARSER_LOG_ERR(ctx, "failed to find field %s while parsing datamap %s", block.symbol, dm->class_name);
            return CH_ERR_FIELD_NOT_FOUND;
        }

        if (field->type <= FIELD_VOID || field->type >= FIELD_TYPECOUNT)
            return CH_ERR_BAD_FIELD_TYPE;

        // read the field!

        if (field->type == FIELD_CUSTOM) {
            CH_PARSER_LOG_ERR(ctx, "CUSTOM fields are not implemented yet (%s.%s)", dm->class_name, field->name);
            ctx->br.cur = ctx->br.end; // skip
        } else if (field->type == FIELD_EMBEDDED) {
            for (int j = 0; j < field->n_elems; j++)
                CH_RET_IF_ERR(ch_br_restore_recursive(ctx,
                                                      field->embedded_map,
                                                      class_ptr + field->ch_offset + field->total_size_bytes * j));
        } else {
            ch_br_restore_simple_field(ctx, class_ptr + field->ch_offset, field);
        }

        CH_RET_IF_ERR(ch_br_end_block(br, &block, true));
    }

    return CH_ERR_NONE;
}

size_t ch_field_type_byte_size(ch_field_type ft)
{
    switch (ft) {
        case FIELD_FLOAT:
        case FIELD_TIME:
            return sizeof(float);
        case FIELD_VECTOR:
        case FIELD_POSITION_VECTOR:
            return 3 * sizeof(float);
        case FIELD_QUATERNION:
            return 4 * sizeof(float);
        case FIELD_BOOLEAN:
        case FIELD_CHARACTER:
            return sizeof(int8_t);
        case FIELD_SHORT:
            return sizeof(int16_t);
        case FIELD_STRING:
        case FIELD_INTEGER:
        case FIELD_COLOR32:
        case FIELD_CLASSPTR:
        case FIELD_EHANDLE:
        case FIELD_EDICT:
        case FIELD_TICK:
        case FIELD_MODELNAME:
        case FIELD_SOUNDNAME:
        case FIELD_INPUT:
        case FIELD_MODELINDEX:
        case FIELD_MATERIALINDEX:
            return sizeof(int32_t);
        case FIELD_FUNCTION:
            return sizeof(void*);
        case FIELD_VECTOR2D:
        case FIELD_INTERVAL:
            return 2 * sizeof(float);
        case FIELD_VMATRIX:
        case FIELD_VMATRIX_WORLDSPACE:
            return 16 * sizeof(float);
        case FIELD_MATRIX3X4_WORLDSPACE:
            return 12 * sizeof(float);
        case FIELD_VOID:
        case FIELD_EMBEDDED:
        case FIELD_CUSTOM:
        case FIELD_TYPECOUNT:
        default:
            assert(0);
            return 0;
    }
}

const char* ch_create_field_type_str(const ch_type_description* td)
{
    const char* base_type_str = NULL;

#define CH_BASE_STR_CASE(type, str) \
    case type:                      \
        base_type_str = str;        \
        break

    switch (td->type) {
        CH_BASE_STR_CASE(FIELD_FLOAT, "f32");
        CH_BASE_STR_CASE(FIELD_STRING, "string");
        CH_BASE_STR_CASE(FIELD_VECTOR, "vec3f");
        CH_BASE_STR_CASE(FIELD_QUATERNION, "quaternion");
        CH_BASE_STR_CASE(FIELD_INTEGER, "i32");
        CH_BASE_STR_CASE(FIELD_BOOLEAN, "bool");
        CH_BASE_STR_CASE(FIELD_SHORT, "i16");
        CH_BASE_STR_CASE(FIELD_CHARACTER, "char");
        CH_BASE_STR_CASE(FIELD_COLOR32, "color32");
        CH_BASE_STR_CASE(FIELD_CLASSPTR, "ent_index");
        CH_BASE_STR_CASE(FIELD_EHANDLE, "ehandle");
        CH_BASE_STR_CASE(FIELD_EDICT, "edict_t");
        CH_BASE_STR_CASE(FIELD_POSITION_VECTOR, "vec3f");
        CH_BASE_STR_CASE(FIELD_TIME, "time");
        CH_BASE_STR_CASE(FIELD_TICK, "tick");
        CH_BASE_STR_CASE(FIELD_MODELNAME, "model");
        CH_BASE_STR_CASE(FIELD_SOUNDNAME, "sound_name");
        CH_BASE_STR_CASE(FIELD_FUNCTION, "func_ptr");
        CH_BASE_STR_CASE(FIELD_VMATRIX, "VMatrix");
        CH_BASE_STR_CASE(FIELD_VMATRIX_WORLDSPACE, "VMatrix");
        CH_BASE_STR_CASE(FIELD_MATRIX3X4_WORLDSPACE, "matrix3x4_t");
        CH_BASE_STR_CASE(FIELD_INTERVAL, "interval");
        CH_BASE_STR_CASE(FIELD_MODELINDEX, "model");
        CH_BASE_STR_CASE(FIELD_MATERIALINDEX, "material");
        CH_BASE_STR_CASE(FIELD_VECTOR2D, "vec2f");
        case FIELD_CUSTOM:
            return "CUSTOM";
        case FIELD_EMBEDDED:
            return "EMBEDDED";
        case FIELD_TYPECOUNT:
        case FIELD_INPUT:
        case FIELD_VOID:
        default:
            return "UNKNOWN";
    }

#undef CH_BASE_STR_CASE

    if (td->n_elems == 1)
        return base_type_str;

    static char buf[32];
    int len = snprintf(buf, sizeof buf, "%s[%d]", base_type_str, td->n_elems);
    assert(len > 0 && len < sizeof(buf));
    return buf;
}

// TODO expose to user
static ch_field_type ch_reduce_field_type_for_printing(ch_field_type ft)
{
#define CH_REDUCE_CASE(from, to) \
    case from:                   \
        return to

    switch (ft) {
        CH_REDUCE_CASE(FIELD_FLOAT, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_STRING, FIELD_STRING);
        CH_REDUCE_CASE(FIELD_VECTOR, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_QUATERNION, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_INTEGER, FIELD_INTEGER);
        CH_REDUCE_CASE(FIELD_BOOLEAN, FIELD_BOOLEAN);
        CH_REDUCE_CASE(FIELD_SHORT, FIELD_SHORT);
        CH_REDUCE_CASE(FIELD_CHARACTER, FIELD_CHARACTER);
        CH_REDUCE_CASE(FIELD_COLOR32, FIELD_COLOR32);
        CH_REDUCE_CASE(FIELD_CLASSPTR, FIELD_INTEGER);
        CH_REDUCE_CASE(FIELD_EHANDLE, FIELD_EHANDLE);
        CH_REDUCE_CASE(FIELD_EDICT, FIELD_INTEGER);
        CH_REDUCE_CASE(FIELD_POSITION_VECTOR, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_TIME, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_TICK, FIELD_INTEGER);
        CH_REDUCE_CASE(FIELD_MODELNAME, FIELD_STRING);
        CH_REDUCE_CASE(FIELD_SOUNDNAME, FIELD_STRING);
        CH_REDUCE_CASE(FIELD_FUNCTION, FIELD_STRING);
        CH_REDUCE_CASE(FIELD_VMATRIX, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_VMATRIX_WORLDSPACE, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_MATRIX3X4_WORLDSPACE, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_INTERVAL, FIELD_FLOAT);
        CH_REDUCE_CASE(FIELD_MODELINDEX, FIELD_STRING);
        CH_REDUCE_CASE(FIELD_MATERIALINDEX, FIELD_STRING);
        CH_REDUCE_CASE(FIELD_VECTOR2D, FIELD_FLOAT);
        case FIELD_VOID:
        case FIELD_EMBEDDED:
        case FIELD_CUSTOM:
        case FIELD_INPUT:
        case FIELD_TYPECOUNT:
        default:
            assert(0);
            return FIELD_VOID;
    }

#undef CH_REDUCE_CASE
}

static void* ch_memnz(void* mem, size_t n_max)
{
    for (unsigned char* m = mem; m < (unsigned char*)mem + n_max; m++)
        if (*m)
            return m;
    return NULL;
}

ch_err ch_dump_restored_class_text(ch_dump_text* dump, ch_restored_class* class);

static ch_err ch_dump_field_to_text(ch_dump_text* dump,
                                    const ch_type_description* td,
                                    unsigned char* field_ptr,
                                    bool* found)
{
    if (td->type == FIELD_CUSTOM) {
        *found = true;
        return ch_dump_text_printf(dump, "CUSTOM %s: (NOT IMPLEMENTED)\n", td->name);
    }
    if (td->type == FIELD_EMBEDDED) {
        *found = true;
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "embedded "));
        ch_restored_class class = {.dm = td->embedded_map, .data = field_ptr};
        return ch_dump_restored_class_text(dump, &class);
    }
    if ((dump->flags & CH_DF_IGNORE_ZERO_FIELDS) && !ch_memnz(field_ptr, td->total_size_bytes))
        return CH_ERR_NONE;
    *found = true;
    CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s %s: ", ch_create_field_type_str(td), td->name));
    ch_field_type ft_reduced = ch_reduce_field_type_for_printing(td->type);
    size_t reduced_byte_size = ch_field_type_byte_size(ft_reduced);
    size_t n_reduced_elems = td->total_size_bytes / reduced_byte_size;

    if (ft_reduced == FIELD_CHARACTER) {
        // determine if a char array is a printable ascii string
        /*bool is_ascii = true;
        size_t i = 0;
        for (; i < td->total_size_bytes && field_ptr[i] && is_ascii; i++)
            if (!isspace(field_ptr[i]) && !isprint(field_ptr[i]))
                is_ascii = false;
        if (is_ascii)
            is_ascii = !ch_memnz(field_ptr + i, td->total_size_bytes - i);
        // it's ascii - treat as a string, otherwise print the vals as hex

        if (is_ascii)*/
        return ch_dump_text_printf(dump, "\"%.*s\"\n", td->total_size_bytes, field_ptr);

        // TODO doesn't seem like there's a sensible way to figure out if this is a string...
        // add some logic to manually display this in bytes for certain fields
    }

    bool display_as_array = n_reduced_elems > 1 && ft_reduced != FIELD_CHARACTER;
    if (display_as_array)
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "["));
    if (ft_reduced == FIELD_CHARACTER)
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "0x"));

    struct {
        union {
            uint8_t* u8;
            char** s;
            float* f;
            int16_t* i16;
            int32_t* i32;
            uint32_t* u32;
        };
    } field_holder = {.u8 = field_ptr};

    for (size_t i = 0; i < n_reduced_elems; i++) {
#pragma warning(push)
#pragma warning(disable : 4061)
        switch (ft_reduced) {
            case FIELD_FLOAT:
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%f", field_holder.f[i]));
                break;
            case FIELD_STRING:
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s", field_holder.s[i] ? field_holder.s[i] : "<null>"));
                break;
            case FIELD_INTEGER:
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%" PRId32, field_holder.i32[i]));
                break;
            case FIELD_SHORT:
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%" PRId16, field_holder.i32[i]));
                break;
            case FIELD_BOOLEAN:
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s", field_holder.u8[i] ? "true" : "false"));
                break;
            case FIELD_CHARACTER:
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%02" PRIX8, field_holder.u8[i]));
                break;
            case FIELD_EHANDLE:
                uint32_t eh = field_holder.u32[i];
                if (eh == CH_SF_INVALID)
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "{null}"));
                else
                    CH_RET_IF_ERR(ch_dump_text_printf(dump,
                                                      "{index: %" PRIu32 ", serial: %" PRIu32 "}",
                                                      eh & CH_ENT_ENTRY_MASK,
                                                      eh >> CH_NUM_ENT_ENTRY_BITS));
                break;
            case FIELD_COLOR32:
                uint32_t c32 = field_holder.u32[i];
                CH_RET_IF_ERR(ch_dump_text_printf(dump,
                                                  "{r: %" PRIu32 ", g: %" PRIu32 ", b: %" PRIu32 ", a: %" PRIu32 "}",
                                                  c32 >> 24,
                                                  (c32 >> 16) & 255,
                                                  (c32 >> 8) & 255,
                                                  c32 & 255));
                break;
            default:
                assert(0);
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "UNKNOWN"));
                break;
        }
#pragma warning(pop)
        if (display_as_array)
            CH_RET_IF_ERR(ch_dump_text_printf(dump, ", "));
    }

    if (display_as_array)
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "]"));
    return ch_dump_text_printf(dump, "\n");
}

ch_err ch_dump_restored_class_text(ch_dump_text* dump, ch_restored_class* class)
{
    if (dump->flags & CH_DF_SORT_FIELDS_BY_OFFSET) {
        assert(0);
    } else {
        for (const ch_datamap* dm = class->dm; dm; dm = dm->base_map) {
            if (dm == class->dm)
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "class %s:\n", dm->class_name));
            else
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "inherited from %s:\n", dm->class_name));
            dump->indent_lvl++;
            bool any = false;
            for (size_t i = 0; i < dm->n_fields; i++) {
                CH_RET_IF_ERR(ch_dump_field_to_text(dump,
                                                    &dm->fields[i],
                                                    CH_FIELD_AT_PTR(class->data, &dm->fields[i], unsigned char),
                                                    &any));
            }
            if (!any)
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "no fields\n"));
            dump->indent_lvl--;
        }
    }
    return CH_ERR_NONE;
}

const ch_dump_truck g_dump_restored_class_fns = {
    .text = ch_dump_restored_class_text,
};
