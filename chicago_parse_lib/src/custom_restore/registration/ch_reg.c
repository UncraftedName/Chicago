#include "ch_reg.h"
#include "ch_reg_decl.h"
#include "ch_save_internal.h"

typedef enum ch_hm_op_entry_type {
    CH_HM_FIELD_TO_GAME_OPS,
    CH_HM_GAME_OPS_TO_CHICAGO_OPS,
} ch_hm_op_entry_type;

typedef struct ch_hm_ops_entry {
    size_t is_op_to_op : 1;
    size_t is_td : 1;
    size_t game_ops_offset : 30;

    union {
        struct {
            const char* module_name;
            const char* class_name;
            union {
                const char* field_name;
                ch_type_description* td;
            };
        };
        const ch_custom_ops* chicago_ops;
    };
} ch_hm_ops_entry;

static uint64_t ch_ops_entry_hash(const void* item, uint64_t seed0, uint64_t seed1)
{
    const ch_hm_ops_entry* e = (const ch_hm_ops_entry*)item;
    if (e->is_op_to_op)
        return e->game_ops_offset;
    uint64_t hash = hashmap_xxhash3(e->module_name, strlen(e->module_name), seed0, seed1) ^
                    hashmap_xxhash3(e->class_name, strlen(e->class_name), seed0, seed1);
    const char* s = e->is_td ? e->td->name : e->field_name;
    return hash ^ hashmap_xxhash3(s, strlen(s), seed0, seed1);
}

#define CH_INT_CMP(a, b) (((a) > (b)) - ((a) < (b)))

static int ch_ops_entry_compare(const void* a, const void* b, void* udata)
{
    (void)udata;
    const ch_hm_ops_entry* ea = (const ch_hm_ops_entry*)a;
    const ch_hm_ops_entry* eb = (const ch_hm_ops_entry*)b;
    if (ea->is_op_to_op != ea->is_op_to_op)
        return CH_INT_CMP(ea->is_op_to_op, eb->is_op_to_op);
    if (ea->is_op_to_op)
        return CH_INT_CMP(ea->game_ops_offset, eb->game_ops_offset);
    int ret = strcmp(ea->module_name, ea->module_name);
    if (ret)
        return ret;
    ret = strcmp(ea->class_name, eb->class_name);
    if (ret)
        return ret;
    const char* sa = ea->is_td ? ea->td->name : ea->field_name;
    const char* sb = eb->is_td ? eb->td->name : eb->field_name;
    return strcmp(sa, sb);
}

typedef struct ch_custom_builder {
    struct hashmap* hm;
} ch_custom_builder;

static ch_err ch_register_cb(ch_register_info* info)
{
    ch_hm_ops_entry field_to_ops_get = {
        .module_name = info->ex_module_name,
        .class_name = info->ex_class_name,
        .field_name = info->ex_field_name,
        .is_op_to_op = false,
        .is_td = false,
    };

    const ch_hm_ops_entry* field_to_ops = hashmap_get(info->builder->hm, &field_to_ops_get);
    if (!field_to_ops)
        return CH_ERR_NONE; // game doesn't have this field (or the names were misspelled :p)

    ch_hm_ops_entry op_to_op = {
        .game_ops_offset = field_to_ops->game_ops_offset,
        .chicago_ops = info->ops,
        .is_op_to_op = true,
    };
    const ch_hm_ops_entry* existing = hashmap_set(info->builder->hm, &op_to_op);
    // if this is triggered then custom fields tried to register different ops for the same field
    assert(!existing);
    return hashmap_oom(info->builder->hm) ? CH_ERR_OUT_OF_MEMORY : CH_ERR_NONE;
}

ch_err ch_register_all(ch_datamap_collection_header* header, ch_datamap_collection* collection)
{
    const ch_custom_register register_fns[] = {
        ch_utl_vec_register,
    };

    struct hashmap* hm = hashmap_new(sizeof(ch_hm_ops_entry),
                                     header->n_datamaps,
                                     0,
                                     0,
                                     ch_ops_entry_hash,
                                     ch_ops_entry_compare,
                                     NULL,
                                     NULL);

    if (!hm)
        return CH_ERR_OUT_OF_MEMORY;
    for (size_t i = 0 ; i < header->n_datamaps; i++) {
        ch_datamap* dm = (ch_datamap*)&header->dms[i];
        for (size_t j = 0; j < dm->n_fields; j++) {
            ch_type_description* td = (ch_type_description*)&dm->fields[j];
            if (td->save_restore_ops_rel_off == CH_REL_OFF_NULL) {
                td->save_restore_ops = NULL;
                continue;
            }
            ch_hm_ops_entry entry = {
                .module_name = dm->module_name,
                .class_name = dm->class_name,
                .td = td,
                .is_op_to_op = false,
                .is_td = true,
                .game_ops_offset = td->save_restore_ops_rel_off,
            };
            const void* cur_entry = hashmap_set(hm, &entry);
            assert(!cur_entry);
            if (hashmap_oom(hm)) {
                hashmap_free(hm);
                return CH_ERR_OUT_OF_MEMORY;
            }
        }
    }

    ch_custom_builder builder = {.hm = hm};

    ch_register_params params = {
        .cb = ch_register_cb,
        .builder = &builder,
        .collection = collection,
    };

    for (size_t i = 0; i < CH_ARRAYSIZE(register_fns); i++) {
        ch_err err = register_fns[i](&params);
        if (err) {
            hashmap_free(hm);
            return err;
        }
    }

    size_t _it = 0;
    ch_hm_ops_entry* builder_entry = NULL;
    while (hashmap_iter(builder.hm, &_it, &builder_entry)) {
        if (builder_entry->is_op_to_op)
            continue;
        assert(builder_entry->is_td);
        ch_hm_ops_entry op_to_op_get = {
            .game_ops_offset = builder_entry->game_ops_offset,
            .is_op_to_op = true,
        };
        const ch_hm_ops_entry* op_to_op = hashmap_get(hm, &op_to_op_get);
        builder_entry->td->save_restore_ops = op_to_op ? op_to_op->chicago_ops : NULL;
    }

    hashmap_free(builder.hm);

    return CH_ERR_NONE;
}
