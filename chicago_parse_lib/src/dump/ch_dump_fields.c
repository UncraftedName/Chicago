#include <inttypes.h>

#include "ch_dump_decl.h"

// TODO expose everything

#define CH_INVALID_HANDLE_INDEX 0xFFFFFFFF
#define CH_MAX_EDICT_BITS 11
#define CH_MAX_EDICTS (1 << CH_MAX_EDICT_BITS)
#define CH_NUM_ENT_ENTRY_BITS (CH_MAX_EDICT_BITS + 1)
#define CH_NUM_ENT_ENTRIES (1 << CH_NUM_ENT_ENTRY_BITS)
#define CH_ENT_ENTRY_MASK (CH_NUM_ENT_ENTRIES - 1)
#define CH_NUM_SERIAL_NUM_BITS (32 - CH_NUM_ENT_ENTRY_BITS)

const char* ch_field_type_string(ch_field_type ft)
{
#define CH_FT_CASE(type, str) \
    case type:                \
        return str;           \
        break

    switch (ft) {
        CH_FT_CASE(FIELD_FLOAT, "f32");
        CH_FT_CASE(FIELD_STRING, "string");
        CH_FT_CASE(FIELD_VECTOR, "vec3f");
        CH_FT_CASE(FIELD_QUATERNION, "quaternion");
        CH_FT_CASE(FIELD_INTEGER, "i32");
        CH_FT_CASE(FIELD_BOOLEAN, "bool");
        CH_FT_CASE(FIELD_SHORT, "i16");
        CH_FT_CASE(FIELD_CHARACTER, "char");
        CH_FT_CASE(FIELD_COLOR32, "color32");
        CH_FT_CASE(FIELD_CLASSPTR, "ent_index");
        CH_FT_CASE(FIELD_EHANDLE, "ehandle");
        CH_FT_CASE(FIELD_EDICT, "edict_t");
        CH_FT_CASE(FIELD_POSITION_VECTOR, "vec3f");
        CH_FT_CASE(FIELD_TIME, "time");
        CH_FT_CASE(FIELD_TICK, "tick");
        CH_FT_CASE(FIELD_MODELNAME, "model");
        CH_FT_CASE(FIELD_SOUNDNAME, "sound_name");
        CH_FT_CASE(FIELD_FUNCTION, "func_ptr");
        CH_FT_CASE(FIELD_VMATRIX, "VMatrix");
        CH_FT_CASE(FIELD_VMATRIX_WORLDSPACE, "VMatrix");
        CH_FT_CASE(FIELD_MATRIX3X4_WORLDSPACE, "matrix3x4_t");
        CH_FT_CASE(FIELD_INTERVAL, "interval");
        CH_FT_CASE(FIELD_MODELINDEX, "model");
        CH_FT_CASE(FIELD_MATERIALINDEX, "material");
        CH_FT_CASE(FIELD_VECTOR2D, "vec2f");
        case FIELD_CUSTOM:
            return "CUSTOM";
        case FIELD_EMBEDDED:
            return "EMBEDDED";
        case FIELD_VOID:
            return "VOID";
        case FIELD_TYPECOUNT:
        case FIELD_INPUT:
        default:
            return "UNKNOWN";
    }

#undef CH_FT_CASE
}

// make sure to save the string before calling again
static const char* ch_create_field_type_str(const ch_type_description* td)
{
    const char* base_type_str = ch_field_type_string(td->type);
    if (td->n_elems == 1)
        return base_type_str;
    static char buf[32];
    int len = snprintf(buf, sizeof buf, "%s[%d]", base_type_str, td->n_elems);
    assert(len > 0 && len < sizeof(buf));
    return buf;
}

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

static const void* ch_memnz(const void* mem, size_t n_max)
{
    for (const unsigned char* m = mem; m < (const unsigned char*)mem + n_max; m++)
        if (*m)
            return m;
    return NULL;
}

ch_err ch_dump_field_val_text(ch_dump_text* dump,
                              ch_field_type ft,
                              size_t total_size_bytes,
                              const void* field_ptr,
                              bool always_show_as_array)
{
    ch_field_type ft_reduced = ch_reduce_field_type_for_printing(ft);
    size_t reduced_byte_size = ch_field_type_byte_size(ft_reduced);
    size_t n_reduced_elems = total_size_bytes / reduced_byte_size;

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

        // for now, we'll pretend that one character fields are not strings
        if (n_reduced_elems != 1)
            return ch_dump_text_printf(dump, "\"%.*s\"\n", total_size_bytes, field_ptr);

        // TODO doesn't seem like there's a sensible way to figure out if this is a string...
        // add some logic to manually display this in bytes for certain fields
    }

    bool display_as_array = always_show_as_array || (n_reduced_elems > 1 && ft_reduced != FIELD_CHARACTER);
    if (display_as_array)
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "["));
    if (ft_reduced == FIELD_CHARACTER)
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "0x"));

    struct {
        union {
            const uint8_t* u8;
            const char** s;
            const float* f;
            const int16_t* i16;
            const int32_t* i32;
            const uint32_t* u32;
        };
    } field_holder = {.u8 = field_ptr};

    for (size_t i = 0; i < n_reduced_elems; i++) {
#pragma warning(push)
#pragma warning(disable : 4061)
        switch (ft_reduced) {
            case FIELD_FLOAT:
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "%#g", field_holder.f[i]));
                break;
            case FIELD_STRING:
                if (field_holder.s[i])
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "\"%s\"", field_holder.s[i]));
                else
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "<null>"));
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
                if (eh == CH_INVALID_HANDLE_INDEX)
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
        if (display_as_array && i != n_reduced_elems - 1)
            CH_RET_IF_ERR(ch_dump_text_printf(dump, ", "));
    }

    if (display_as_array)
        CH_RET_IF_ERR(ch_dump_text_printf(dump, "]"));
    return ch_dump_text_printf(dump, "\n");
}

static ch_err ch_dump_restored_fields_text(ch_dump_text* dump, const ch_datamap* dm, const unsigned char* data)
{
    if (dump->flags & CH_DF_SORT_FIELDS_BY_OFFSET) {
        assert(0);
    } else {
        for (const ch_datamap* dm_it = dm; dm_it; dm_it = dm_it->base_map) {
            if (dm_it != dm)
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "inherited from %s:\n", dm_it->class_name));
            dump->indent_lvl++;
            bool any = false;
            for (size_t i = 0; i < dm_it->n_fields; i++) {
                const ch_type_description* td = &dm_it->fields[i];
                const unsigned char* field_ptr = CH_FIELD_AT_PTR(data, td, unsigned char);

                if ((dump->flags & CH_DF_IGNORE_ZERO_FIELDS) && !ch_memnz(field_ptr, td->total_size_bytes))
                    continue;

                any = true;

                if (td->type == FIELD_CUSTOM) {
                    const void** custom_ptr = (const void**)field_ptr;
                    if (td->save_restore_ops) {
                        if (*custom_ptr) {
                            CH_RET_IF_ERR(CH_DUMP_TEXT_CALL(*td->save_restore_ops->dump_fns, dump, td, *custom_ptr));
                        } else {
                            CH_RET_IF_ERR(ch_dump_text_printf(dump, "CUSTOM %s: <null>\n", td->name));
                        }
                    } else {
                        // print NOT IMPLEMENTED even if the custom field is null - this may be because the field wasn't parsed
                        CH_RET_IF_ERR(ch_dump_text_printf(dump, "CUSTOM %s: (NOT IMPLEMENTED)\n", td->name));
                    }
                    continue;
                }
                if (td->type == FIELD_EMBEDDED) {
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s %s:\n", td->embedded_map->class_name, td->name));
                    CH_RET_IF_ERR(ch_dump_restored_fields_text(dump, td->embedded_map, field_ptr));
                } else {
                    CH_RET_IF_ERR(ch_dump_text_printf(dump, "%s %s: ", ch_create_field_type_str(td), td->name));
                    CH_RET_IF_ERR(ch_dump_field_val_text(dump, td->type, td->total_size_bytes, field_ptr, false));
                }
            }
            if (!any)
                CH_RET_IF_ERR(ch_dump_text_printf(dump, "no fields\n"));
            dump->indent_lvl--;
        }
    }
    return CH_ERR_NONE;
}

static ch_err ch_dump_restored_class_text(ch_dump_text* dump, const ch_datamap* dm, const unsigned char* data)
{
    CH_RET_IF_ERR(ch_dump_text_printf(dump, "class %s:\n", dm->class_name));
    return ch_dump_restored_fields_text(dump, dm, data);
}

const ch_dump_restored_class_fns g_dump_restored_class_fns = {
    .text = ch_dump_restored_class_text,
};
