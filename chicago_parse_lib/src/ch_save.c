#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ch_field_reader.h"
#include "ch_save_internal.h"

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
            return sizeof(uint8_t);
        case FIELD_SHORT:
            return sizeof(uint16_t);
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
            return sizeof(uint32_t);
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

ch_err ch_parse_save_log_error(ch_parsed_save_ctx* ctx, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int len = vsnprintf(NULL, 0, fmt, va);
    va_end(va);
    ch_parse_save_error* pse = ch_arena_alloc(ctx->arena, sizeof(ch_parse_save_error) + (len < 0 ? 0 : len + 1));
    if (!pse)
        return CH_ERR_OUT_OF_MEMORY;
    if (len < 0) {
        pse->err_str = "ERROR ENCODING LOG STRING";
    } else {
        pse->err_str = (const char*)(pse + 1);
        va_start(va, fmt);
        vsnprintf((char*)pse->err_str, len + 1, fmt, va);
        va_end(va);
    }
    printf("%s\n", pse->err_str);
    if (ctx->last_error)
        ctx->last_error->next = pse;
    ctx->last_error = pse;
    if (!ctx->data->errors_ll)
        ctx->data->errors_ll = pse;
    pse->next = NULL;
    return CH_ERR_NONE;
}

ch_err ch_parse_save_bytes(ch_parsed_save_data* parsed_data, const ch_parse_info* info)
{
    ch_parsed_save_ctx ctx = {
        .info = info,
        .data = parsed_data,
        .br =
            {
                .cur = info->bytes,
                .end = (unsigned char*)info->bytes + info->n_bytes,
                .overflowed = false,
            },
        .arena = parsed_data->_arena,
    };
    return ch_parse_save_ctx(&ctx);
}

ch_err ch_find_field(const ch_datamap* dm,
                     const char* field_name,
                     bool recurse_base_classes,
                     const ch_type_description** field)
{
    assert(field_name);
    if (!field)
        return CH_ERR_NONE;
    for (; dm; dm = dm->base_map) {
        for (size_t i = 0; i < dm->n_fields; i++) {
            if (!strcmp(dm->fields[i].name, field_name)) {
                *field = &dm->fields[i];
                return CH_ERR_NONE;
            }
        }
        if (!recurse_base_classes)
            break;
    }
    *field = NULL;
    return CH_ERR_FIELD_NOT_FOUND;
}

bool ch_dm_inherts_from(const ch_datamap* dm, const char* base_name)
{
    if (!dm)
        return false;
    if (!strcmp(dm->class_name, base_name))
        return true;
    return ch_dm_inherts_from(dm->base_map, base_name);
}

ch_err ch_find_field_log_if_dne(ch_parsed_save_ctx* ctx,
                                const ch_datamap* dm,
                                const char* field_name,
                                bool recurse_base_classes,
                                const ch_type_description** field,
                                ch_field_type expected_field_type)
{
    assert(field_name && field);
    ch_err err = ch_find_field(dm, field_name, recurse_base_classes, field);
    if (err) {
        CH_PARSER_LOG_ERR(ctx, "failed to find filed '%s' in datamap '%s'", field_name, dm->class_name);
        return err;
    } else if ((**field).type != expected_field_type) {
        CH_PARSER_LOG_ERR(ctx,
                          "found field '%s' in datamap '%s' but it has type %d, expected %d",
                          (**field).name,
                          dm->class_name,
                          (**field).type,
                          expected_field_type);
        return CH_ERR_BAD_FIELD_TYPE;
    }
    return CH_ERR_NONE;
}

ch_err ch_lookup_datamap(ch_parsed_save_ctx* ctx, const char* name, const ch_datamap** dm)
{
    assert(name && dm);
    ch_datamap_lookup_entry entry_in = {.name = name};
    const ch_datamap_lookup_entry* entry_out = hashmap_get(ctx->info->datamap_collection->lookup, &entry_in);
    if (!entry_out) {
        CH_PARSER_LOG_ERR(ctx, "datamap '%s' not found in collection", name);
        *dm = NULL;
        return CH_ERR_DATAMAP_NOT_FOUND;
    }
    *dm = entry_out->datamap;
    return CH_ERR_NONE;
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

    ch_err err = CH_ERR_NONE;

    // read symbol table

    if (st_size_bytes > 0) {
        ch_byte_reader br_st = ch_br_split_skip(br, st_size_bytes);
        if (br->overflowed)
            return CH_ERR_READER_OVERFLOWED;
        CH_RET_IF_ERR(ch_br_read_symbol_table(&br_st, &ctx->st, st_n_symbols));
    }

    // read global fields

    ch_byte_reader br_after_fields = ch_br_split_skip_swap(br, global_fields_size_bytes);
    if (br_after_fields.overflowed) {
        err = CH_ERR_READER_OVERFLOWED;
    } else {
        CH_RET_IF_ERR(ch_br_restore_class_by_name(ctx, "GameHeader", "GAME_HEADER", &ctx->data->game_header));
        CH_RET_IF_ERR(ch_br_restore_class_by_name(ctx, "GLOBAL", "CGlobalState", &ctx->data->global_state));
    }
    *br = br_after_fields;
    ch_free_symbol_table(&ctx->st);
    if (err)
        return err;

    // determine number of state files

    int32_t n_state_files = 0;

    // set the number of state files from the game header
    const ch_type_description* td;
    ch_err find_err = ch_find_field(ctx->data->game_header.dm, "mapCount", true, &td);
    if (!find_err)
        n_state_files = CH_FIELD_AT(ctx->data->game_header.data, td, int32_t);

    // newer implementation stores the number of state files explicitly after the header, see CSaveRestore::SaveGameSlot
    if (n_state_files == 0)
        n_state_files = ch_br_read_32(br);

    if (br->overflowed)
        return CH_ERR_READER_OVERFLOWED;
    if (n_state_files < 0)
        return CC_ERR_BAD_STATE_FILE_COUNT;

    // read state files

    ctx->data->n_state_files = n_state_files;
    ctx->data->state_files = ch_arena_calloc(ctx->arena, n_state_files * sizeof(ch_state_file));
    if (!ctx->data->state_files)
        return CH_ERR_OUT_OF_MEMORY;

    for (int i = 0; i < n_state_files; i++) {
        ch_state_file* sf = &ctx->data->state_files[i];
        if (!ch_br_read(br, sf->name, sizeof(sf->name)))
            return CH_ERR_READER_OVERFLOWED;
        int sf_len_bytes = ch_br_read_32(br);
        if (sf_len_bytes < 0)
            return CH_ERR_BAD_STATE_FILE_LENGTH;
        ch_byte_reader br_after_sf = ch_br_split_skip_swap(br, sf_len_bytes);
        if (br_after_sf.overflowed)
            return CH_ERR_READER_OVERFLOWED;
        CH_RET_IF_ERR(ch_parse_state_file(ctx, sf));
        *br = br_after_sf;
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
        return CC_ERR_BAD_STATE_FILE_NAME;

    if (!strncmp(sf->name + i, ".hl1", 4)) {
        sf->sf_type = CH_SF_SAVE_DATA;
        ctx->sf_save_data = &sf->sf_save_data;
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

ch_parsed_save_data* ch_parsed_save_new(void)
{
    ch_arena* arena = ch_arena_new(max(4096, sizeof(ch_parsed_save_data)));
    if (!arena)
        return NULL;
    ch_parsed_save_data* parsed_data = ch_arena_calloc(arena, sizeof(ch_parsed_save_data));
    if (!parsed_data) {
        ch_arena_free(arena);
        return NULL;
    }
    parsed_data->_arena = arena;
    return parsed_data;
}

// TODO figure out what the hell to do with the symbol tables
void ch_parsed_save_free(ch_parsed_save_data* parsed_data)
{
    ch_arena_free(parsed_data->_arena);
}
