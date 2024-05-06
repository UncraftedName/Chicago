#include "ch_ent_factory.h"
#include "ch_send.h"
#include "thirdparty/x86/x86.h"

#include "SDK/datamap.h"

#define CH_NEXT_INSTRUCTION(ctx, x, fmt, ...)                                                        \
    do {                                                                                             \
        int _len = x86_len(x);                                                                       \
        if (_len == -1)                                                                              \
            CH_PAYLOAD_LOG_ERR(ctx, "unknown or invalid instruction looking for " fmt, __VA_ARGS__); \
        x += _len;                                                                                   \
    } while (0)

typedef struct ch_find_factory_cvar_ctor_cb_udata {
    ch_mod_sec server_mod_text;
    ch_pattern ctor_pattern;
    ch_ptr cvar_desc_str;
    ch_ptr cvar_ctor_out;
    ch_ptr cvar_impl_out;
} ch_find_factory_cvar_ctor_cb_udata;

static bool ch_find_factory_cvar_ctor_cb(ch_ptr match, void* user_data)
{
    ch_find_factory_cvar_ctor_cb_udata* udata = user_data;
    udata->cvar_ctor_out = match - 15;

    if (udata->cvar_ctor_out < udata->server_mod_text.start)
        return false;
    if (!ch_pattern_match(udata->cvar_ctor_out, udata->server_mod_text, udata->ctor_pattern))
        return false;
    if (*(ch_ptr*)(udata->cvar_ctor_out + 5) != udata->cvar_desc_str)
        return false;
    udata->cvar_impl_out = *(ch_ptr*)(udata->cvar_ctor_out + 10);
    if (!ch_ptr_in_sec(udata->cvar_impl_out, udata->server_mod_text, 0))
        return false;
    return true;
}

static ch_ptr ch_find_ent_factory(ch_send_ctx* ctx, const ch_mod_info* server_mod)
{
    ch_mod_sec server_rdata = server_mod->sections[CH_SEC_RDATA];
    ch_mod_sec server_text = server_mod->sections[CH_SEC_TEXT];

    enum {
        CH_CVAR_STR_NAME,
        CH_CVAR_STR_DESC,
    };

    struct {
        const char* str;
        ch_ptr p;
    } str_infos[] = {
        {.str = "dumpentityfactories"},
        {.str = "Lists all entity factory names."},
    };

    // first, find the above strings

    for (int i = 0; i < 2; i++) {
        str_infos[i].p = ch_memmem_unique(server_rdata.start,
                                          server_rdata.len,
                                          (ch_ptr)str_infos[i].str,
                                          strlen(str_infos[i].str) + 1);

        if (str_infos[i].p == NULL || str_infos[i].p == CH_MEM_DUP)
            CH_PAYLOAD_LOG_ERR(ctx,
                               "error searching for string '%s' in the .rdata section in server.dll (%s)",
                               str_infos[i].str,
                               str_infos[i].p == NULL ? "no matches" : "multiple matches but expected 1");
    }

    ch_find_factory_cvar_ctor_cb_udata cb_udata = {
        .server_mod_text = server_text,
        .cvar_desc_str = str_infos[CH_CVAR_STR_DESC].p,
    };
    //                                             v (cvar desc)  v (cvar impl)  v (cvar name)  v (thisptr)
    const char* ctor_pattern_str = "6A 00 6A 04 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? B9 ?? ?? ?? ??";
    unsigned char ctor_pattern_scratch[28];
    ch_parse_pattern_str(ctor_pattern_str, &cb_udata.ctor_pattern, ctor_pattern_scratch);

    ch_ptr found_ctor = ch_memmem_cb(server_text.start,
                                     server_text.len,
                                     (ch_ptr)&str_infos[CH_CVAR_STR_NAME].p,
                                     sizeof(ch_ptr),
                                     ch_find_factory_cvar_ctor_cb,
                                     &cb_udata);

    if (!found_ctor)
        CH_PAYLOAD_LOG_ERR(ctx, "failed to find 'dumpentityfactories' ConCommand ctor in server.dll", "DUMMY");

    assert(cb_udata.cvar_ctor_out && cb_udata.cvar_impl_out);

    CH_PAYLOAD_LOG_INFO(ctx,
                        "found 'dumpentityfactories' ConCommand ctor at server.dll" CH_ADDR_FMT,
                        CH_PTR_DIFF(cb_udata.cvar_ctor_out, server_mod->base));
    return cb_udata.cvar_impl_out;
}

const ch_ent_factory_dict* ch_find_ent_factory_dict(struct ch_send_ctx* ctx, const ch_mod_info* server_mod)
{
    ch_ptr factory_cb = ch_find_ent_factory(ctx, server_mod);

    const ch_ent_factory_dict* efd = NULL;
    bool passed_vcall = false;
    for (ch_ptr p = factory_cb; CH_PTR_DIFF(p, factory_cb) < 64;) {
        if (passed_vcall && p[0] == X86_MOVECXI)
            efd = *(const ch_ent_factory_dict**)(p + 1);
        if (p[0] == X86_MISCMW && (p[1] & 0b11000000) == 0b11000000)
            passed_vcall = true;
        CH_NEXT_INSTRUCTION(ctx,
                            p,
                            "entity factory callback at server.dll" CH_ADDR_FMT,
                            CH_PTR_DIFF(factory_cb, server_mod->base));
    }
    if (!efd)
        CH_PAYLOAD_LOG_ERR(ctx,
                           "failed to find the CEntityFactoryDictionary from the 'dumpentityfactories' callback at "
                           "server.dll" CH_ADDR_FMT,
                           CH_PTR_DIFF(factory_cb, server_mod->base));
    CH_PAYLOAD_LOG_INFO(ctx,
                        "found the CEntityFactoryDictionary at server.dll" CH_ADDR_FMT,
                        CH_PTR_DIFF(efd, server_mod->base));
    return efd;
}

static const datamap_t* ch_get_dm_from_server_ent_vtable(ch_ptr* ent_vt, const ch_mod_info* server_mod)
{
    static int vt_get_data_desc_map_off = -1;
    static ch_pattern pattern = {0};
    static unsigned char scratch[8];

    // init pattern
    if (pattern.len == 0) {
        const char* pstr = "B8 ?? ?? ?? ?? C3";
        ch_parse_pattern_str(pstr, &pattern, scratch);
    }

    // entities have way more than this many virtual funcs, but it's sufficient to only check this many
    const int max_vt_checks = 32;
    if (!ch_ptr_in_sec(ent_vt, server_mod->sections[CH_SEC_RDATA], sizeof(ch_ptr) * max_vt_checks))
        return false;

    if (vt_get_data_desc_map_off == -1) {
        // find the first virtual function which looks like it returns a datamap
        for (int i = 0; i < max_vt_checks && vt_get_data_desc_map_off == -1; i++) {
            if (!ch_ptr_in_sec(ent_vt[i], server_mod->sections[CH_SEC_TEXT], pattern.len))
                return false; // less than max_vt_checks virtual functions? definitely not an entity
            if (ch_pattern_match(ent_vt[i], server_mod->sections[CH_SEC_TEXT], pattern)) {
                const datamap_t* dm = *(const datamap_t**)(ent_vt[i] + 1);
                if (ch_datamap_looks_valid(dm, server_mod)) {
                    vt_get_data_desc_map_off = i;
                    return dm;
                }
            }
        }
        return NULL;
    } else {
        if (!ch_pattern_match(ent_vt[vt_get_data_desc_map_off], server_mod->sections[CH_SEC_TEXT], pattern))
            return false;
        const datamap_t* dm = *(const datamap_t**)(ent_vt[vt_get_data_desc_map_off] + 1);
        return ch_datamap_looks_valid(dm, server_mod) ? dm : NULL;
    }
}

// TODO remove this bool return ?
static bool ch_process_factory_dict_node(ch_send_ctx* ctx,
                                         const ch_search_ctx* sc,
                                         const char* k,
                                         const ch_ent_factory* v)
{
    const size_t byte_search_lim = 64;

    const ch_mod_info* mod = &sc->mods[CH_MOD_SERVER];

    if (!ch_ptr_in_sec(v->vt, mod->sections[CH_SEC_RDATA], sizeof(*v->vt)) ||
        !ch_ptr_in_sec((void*)v->vt->create, mod->sections[CH_SEC_TEXT], byte_search_lim + 15))
        return false;

    ch_ptr func_start = (ch_ptr)v->vt->create;
    const datamap_t* dm = NULL;
    bool seen_jmp = false;
    for (int depth = 0; depth < 3; depth++) {
        ch_ptr next_call_check = NULL;
        for (ch_ptr p = func_start; CH_PTR_DIFF(p, func_start) < 128;) {
            if (seen_jmp && p[0] == X86_MOVMIW && (p[1] & 0b11111000) == 0) {
                ch_ptr* candidate_vt = *(ch_ptr**)(p + 2);
                // we've seen a jmp AND this is a mov immediate (probably our vtable)
                const datamap_t* candidate_dm = ch_get_dm_from_server_ent_vtable(candidate_vt, mod);
                if (candidate_dm)
                    dm = candidate_dm;
            } else if (p[0] == X86_CALL && !next_call_check) {
                // maybe a thunky type-thingy, maybe the ctor, or maybe the base ctor, recurse into the first call we find
                next_call_check = p;
            } else if (p[0] == X86_JZ || p[0] == X86_JNZ) {
                // after a call to new(), there will always be a null check (before the ctor is called),
                // this means whatever call(s) we found before was probably new() and we should ignore it
                seen_jmp = true;
                next_call_check = NULL;
            } else if (p[0] == X86_RET || p[0] == X86_RETI16) {
                break;
            }
            CH_NEXT_INSTRUCTION(ctx, p, "'%s' vtable at server.dll" CH_ADDR_FMT, k, CH_PTR_DIFF(p, mod->base));
        }
        if (dm || !next_call_check)
            break;
        func_start = next_call_check + 5 + *(size_t*)(next_call_check + 1);
    }

    if (!dm)
        CH_PAYLOAD_LOG_ERR(ctx,
                           "failed to find entity datamap for '%s' from create function at server.dll" CH_ADDR_FMT,
                           k,
                           CH_PTR_DIFF(v->vt->create, mod->base));

    if (msgpack_pack_str_with_body(&ctx->mp_pk, k, strlen(k)))
        ch_clean_exit(ctx, 1);
    if (msgpack_pack_str_with_body(&ctx->mp_pk, dm->dataClassName, strlen(dm->dataClassName)))
        ch_clean_exit(ctx, 1);

    // do NOT report info here, that will clear the msgpack buffer...
    /*CH_PAYLOAD_LOG_INFO(ctx,
                        "class '%s' is '%s' from create func at server.dll" CH_ADDR_FMT,
                        k,
                        dm->dataClassName,
                        CH_PTR_DIFF(v->vt->create, mod->base));*/

    return true;
}

// true on success
bool ch_in_order_traverse_factory_dict_tree(ch_send_ctx* ctx,
                                            const ch_search_ctx* sc,
                                            const ch_ent_factory_dict* ef,
                                            uint16_t node_idx,
                                            uint16_t* n_processed)
{
    if (node_idx >= ef->utl_mem.alloc_count)
        return false;
    struct ch_ent_dict_node* node = &ef->nodes[node_idx];
    if (node->links.l != CH_FACTORY_DICT_INVALID_IDX)
        if (!ch_in_order_traverse_factory_dict_tree(ctx, sc, ef, node->links.l, n_processed))
            return false;
    if (!ch_process_factory_dict_node(ctx, sc, node->k, node->v))
        return false;
    ++*n_processed;
    if (node->links.r != CH_FACTORY_DICT_INVALID_IDX)
        return ch_in_order_traverse_factory_dict_tree(ctx, sc, ef, node->links.r, n_processed);
    return true;
}

void ch_send_ent_factory_kv(ch_send_ctx* ctx, const ch_search_ctx* sc, const ch_ent_factory_dict* factory)
{
    if (ch_msg_preamble(ctx, CH_MSG_LINKED_NAME) || msgpack_pack_map(&ctx->mp_pk, factory->count))
        ch_clean_exit(ctx, 1);

    __try {
        if (factory->nodes != factory->utl_mem.mem)
            __leave;
        uint16_t n_processed = 0;
        if (!ch_in_order_traverse_factory_dict_tree(ctx, sc, factory, factory->root, &n_processed))
            __leave;
        if (factory->count != n_processed)
            __leave;
        ch_send_msgpack(ctx);
        return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    CH_PAYLOAD_LOG_ERR(ctx, "failed reading memory of factory data", "DUMMY");
}
