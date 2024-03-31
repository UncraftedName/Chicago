// high level idea:
// for each entity we can get its datamap by following getting the virtual GetDataDescMap() function.
// this won't work for non-ent datamaps, we could potentially search through ALL virtual functions
// and see if they match the datamap pattern.

// we could also look for datamaps which are explicitly passed into ReadFields().

// possibly a better idea:
// - we know which maps are looked up dynamically (probably only entity stuff but idk), these we can chase with virtual functions
// - the names/structs of other datamaps are hardcoded and there's only a finite amount of them, so we can hardcode those into the searcher too
// this technically may not find all datamaps, but it'll find all the ones that are relevant for save parsing

// or we just figure out how the hell to use the .dylib files but that just looks awful

/*

We have 4 options to extract datamaps:

- parse binaries manually
  * this is ideal and not too difficult in theory because all datadescs follow the same code structure
  * hard in practice because the mac binaries are harder to RE (they use relative addressing), and different game versions have different generated code structure
  * we would have to effectively interpret the x86 instructions and execute them ourselves

- load binaries and run the relevant datadesc functions
  * not sure how to resolve the relevant game binary directories (we'll need to specify a game binary dir and mod binary dir)
  * crashes when unloading server/client (some null pointer exception)
  * can't load engine for some reason (there might be some extra dlls it tries to load that aren't part of the game, perhaps dx8/dx9 related?)

- copy game memory and read datamap from there
  * would have to resolve virtual addresses
  * OR we could load the dlls into the same virtual address as the game - but probably can't guarantee that we can do that always
  * only a valid strategy if all pointers in the datamap lie within the dll DATA section (which they might not, check how e.g. "prop_physics" gets linked)

- inject dll
  * 100% will work, but pee pee poo poo
  * use msgpack to convert datamaps to send data back to server
  * 
  * linked entity names are tricky, there's a global factory that can creates entity instances from linked names
  * the only way to get the entity name is to go to the create function and check the vtable address, then lookup the datamap getter that's in that vtable address


TODO: dear lord how the hell do we get linked names (e.g. prop_physics)?
CEntityFactory can be gotten from dumpentityfactories
for finding maps by name - look for where the string is used, find the start of that function and call it (it's protected with a static initializer thingy)

here's a neat idea - look at all the imports in the dll (these will be in .rdata) and fin the last one. the static init list should be right after thatb
use a pattern on this to look for static inits that just do a call(0) - these are datamapinit candidates. 


Use this for datamap lookup? https://github.com/fanf2/qp
*/

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <libloaderapi.h>
#include <processthreadsapi.h>
#pragma warning(push, 3)
#include <DbgHelp.h>
#pragma warning(pop)

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ch_search.h"
#include "ch_payload_comm_shared.h"
#include "ch_send.h"

#pragma comment(lib, "dbghelp.lib")

void ch_parse_pattern_str(const char* str, ch_pattern* out, unsigned char* scratch)
{
    while (isspace(*str))
        str++;
    out->len = 0;
    // first just count the number of bytes in this pattern and make sure it's a valid pattern
    const char* s = str;
    while (*s) {
        for (int i = 0; i < 2; i++) {
            int c = toupper(*s);
            assert(c == '?' || isxdigit(c));
        }
        out->len++;
        s += 2;
        while (isspace(*s))
            s++;
    }
    // now go through it again and fill the scratch buffer
    out->bytes = scratch;
    out->wildmask = scratch + out->len;
    memset(scratch, 0, out->len + (out->len + 7) / 8);
    s = str;
    size_t i = 0;
    while (*s) {
        if (*s == '?') {
            out->wildmask[i / 8] |= 1 << (i & 7);
        } else {
            int c1 = toupper(*s);
            int c2 = toupper(*(s + 1));
            out->bytes[i] |= (c1 >= 'A' ? c1 - 'A' + 10 : c1 - '0') << 4;
            out->bytes[i] |= c2 >= 'A' ? c2 - 'A' + 10 : c2 - '0';
        }
        i++;
        s += 2;
        while (isspace(*s))
            s++;
    }
    assert(i == out->len);
}

bool ch_pattern_match(ch_ptr mem, ch_pattern pattern)
{
    for (size_t i = 0; i < pattern.len; i++)
        if (!(pattern.wildmask[i / 8] & (1 << (i & 7))) && pattern.bytes[i] != mem[i])
            return false;
    return true;
}

void ch_get_module_info(ch_send_ctx* ctx, ch_search_ctx* sc)
{
    BYTE* base_addresses[CH_MOD_COUNT];
    if (!ch_get_required_modules(0, base_addresses))
        ch_send_err_and_exit(ctx, "Failed to find required modules");

    for (int i = 0; i < CH_MOD_COUNT; i++) {
        ch_mod_info* mod_info = &sc->mods[i];
        mod_info->base = base_addresses[i];
        unsigned int sec_flags = 0, target_flags = (1 << CH_SEC_COUNT) - 1;
        PIMAGE_NT_HEADERS header = ImageNtHeader(base_addresses[i]);
        PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(header);
        for (int fs = 0; fs < header->FileHeader.NumberOfSections && sec_flags != target_flags; fs++) {
            for (int ms = 0; ms < CH_SEC_COUNT; ms++) {
                if (!_stricmp((const char*)section->Name, ch_mod_sec_names[ms])) {
                    mod_info->sections[ms].start = base_addresses[i] + section->VirtualAddress;
                    mod_info->sections[ms].len = section->Misc.VirtualSize;
                    sec_flags |= 1 << ms;
                    break;
                }
            }
            ++section;
        }
        if (sec_flags != target_flags)
            ch_send_err_and_exit(ctx, "Failed to find required sections in %s", ch_mod_sec_names[i]);
    }
}

typedef struct _find_ctor_cb_info {
    ch_mod_sec server_mod_text;
    ch_pattern ctor_pattern;
    ch_ptr cvar_desc_str;
    ch_ptr cvar_ctor_out;
    ch_ptr cvar_impl_out;
} _find_ctor_cb_info;

static bool _find_ctor_cb(ch_ptr match, void* user_data)
{
    _find_ctor_cb_info* info = user_data;
    info->cvar_ctor_out = match - 15;

    if (info->cvar_ctor_out < info->server_mod_text.start)
        return false;
    if (!ch_ptr_in_sec(info->cvar_ctor_out + info->ctor_pattern.len, info->server_mod_text))
        return false;
    if (!ch_pattern_match(info->cvar_ctor_out, info->ctor_pattern))
        return false;
    if (*(ch_ptr*)(info->cvar_ctor_out + 5) != info->cvar_desc_str)
        return false;
    info->cvar_impl_out = *(ch_ptr*)(info->cvar_ctor_out + 10);
    if (!ch_ptr_in_sec(info->cvar_impl_out, info->server_mod_text))
        return false;
    return true;
}

void ch_find_entity_factory_cvar(ch_send_ctx* ctx, ch_search_ctx* sc)
{
    ch_mod_info* server_mod = &sc->mods[CH_MOD_SERVER];
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
            ch_send_err_and_exit(ctx,
                                 "Error searching for string '%s' in the .rdata section in server.dll (%s).",
                                 str_infos[i].str,
                                 str_infos[i].p == NULL ? "no matches" : "multiple matches but expected 1");
    }

    _find_ctor_cb_info cb_info = {
        .server_mod_text = server_text,
        .cvar_desc_str = str_infos[CH_CVAR_STR_DESC].p,
    };
    //                                             v (cvar desc)  v (cvar impl)  v (cvar name)  v (thisptr)
    const char* ctor_pattern_str = "6A 00 6A 04 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? B9 ?? ?? ?? ??";
    unsigned char ctor_pattern_scratch[28];
    ch_parse_pattern_str(ctor_pattern_str, &cb_info.ctor_pattern, ctor_pattern_scratch);

    ch_ptr found_ctor = ch_memmem_cb(server_text.start,
                                     server_text.len,
                                     (ch_ptr)&str_infos[CH_CVAR_STR_NAME].p,
                                     sizeof(ch_ptr),
                                     _find_ctor_cb,
                                     &cb_info);

    if (!found_ctor)
        ch_send_err_and_exit(ctx, "Could not find 'dumpentityfactories' ConCommand ctor in server.dll.");

    assert(cb_info.cvar_ctor_out && cb_info.cvar_impl_out);

    ch_send_log_info(ctx,
                     "Found 'dumpentityfactories' ConCommand ctor at server.dll[0x%08X].",
                     CH_PTR_DIFF(cb_info.cvar_ctor_out, server_mod->base));
    sc->dump_entity_factories_ctor = cb_info.cvar_ctor_out;
    sc->dump_entity_factories_impl = cb_info.cvar_impl_out;
}

typedef struct _find_static_inits_cb_info {
    ch_mod_info* mod;
    ch_ptr table_start_out;
    size_t table_len_out;
} _find_static_inits_cb_info;

static bool _find_static_inits_cb(ch_ptr match, void* user_data)
{
    _find_static_inits_cb_info* info = user_data;
    ch_mod_sec mod_rdata = info->mod->sections[CH_SEC_RDATA];
    ch_mod_sec mod_text = info->mod->sections[CH_SEC_TEXT];

    /*
    * We expect to land somewhere in the static init table. The table is in rdata,
    * and it's just a table that points to a bunch of static init functions surrounded
    * by null terminators. Scan before & after the match, check that each function
    * pointer points to somewhere in the text section.
    */
    const int direction[] = {-1, 1};
    int n_funcs = 0;
    for (int i = 0; i < 2; i++) {
        const ch_ptr* func_pptr = (ch_ptr*)match;
        for (;; n_funcs++) {
            func_pptr += direction[i];
            if (!ch_ptr_in_sec((ch_ptr)func_pptr, mod_rdata))
                return false;
            if (!*func_pptr)
                break;
            if (!ch_ptr_in_sec(*func_pptr, mod_text))
                return false;
        }
        if (i == 0)
            info->table_start_out = (ch_ptr)(func_pptr + 1);
        else
            info->table_len_out = CH_PTR_DIFF((ch_ptr)(func_pptr - 1), info->table_start_out) / sizeof(ch_ptr);
    }
    return n_funcs > 5; // arbitrary threshold - should have at least this many static inits
}

void ch_find_datamap_init_candidates(struct ch_send_ctx* ctx,
                                     ch_search_ctx* sc,
                                     ch_game_module mod_idx,
                                     ch_ptr static_init_func)
{
    ch_mod_info* mod = &sc->mods[mod_idx];
    ch_mod_sec mod_rdata = mod->sections[CH_SEC_RDATA];
    ch_mod_sec mod_text = mod->sections[CH_SEC_TEXT];
    _find_static_inits_cb_info cb_info = {.mod = mod};
    ch_ptr found_table = ch_memmem_cb(mod_rdata.start,
                                      mod_rdata.len,
                                      (ch_ptr)&static_init_func,
                                      sizeof(ch_ptr),
                                      _find_static_inits_cb,
                                      &cb_info);

    if (!found_table)
        ch_send_err_and_exit(ctx, "Failed to find static init table in %s.", ch_mod_names[mod_idx]);
    ch_send_log_info(ctx,
                     "Found %lu static init functions at %s[0x%08X].",
                     cb_info.table_len_out,
                     ch_mod_names[mod_idx],
                     CH_PTR_DIFF(cb_info.table_start_out, mod->base));
    mod->datamap_init_candidates = malloc(sizeof(ch_ptr) * cb_info.table_len_out);
    if (!mod->datamap_init_candidates)
        ch_send_err_and_exit(ctx, "Out of memory (static init table).");
    memcpy(mod->datamap_init_candidates, cb_info.table_start_out, sizeof(ch_ptr) * cb_info.table_len_out);

    /*
    * The static init table has functions of the type (void*)(void), but the DataDescInit functions
    * call the DataMapInit functions with a null param. This is great, because it means the DataMapInit
    * function is not inlined (at least from what I've seen) and has a very consistent and simple
    * pattern. The pattern is just:
    * 
    * push 0
    * call
    * add esp, 4
    * mov [mem], eax
    * ret
    * 
    * So we filter through the static inits and find which ones match this pattern. These are of
    * course not guaranteed to be DataMapInit functions, but they narrow down the search a lot.
    */

    const char* pattern_str = "6A 00 E8 ?? ?? ?? ?? 83 C4 04 A3 ?? ?? ?? ?? C3";
    unsigned char pattern_scratch[18];
    ch_pattern datadescinit_pattern;
    ch_parse_pattern_str(pattern_str, &datadescinit_pattern, pattern_scratch);

    mod->n_datamap_init_candidates = 0;
    for (size_t i = 0; i < cb_info.table_len_out; i++) {
        // we know the function is in .text, make sure the whole pattern is too
        if (!ch_ptr_in_sec(mod->datamap_init_candidates[i] + datadescinit_pattern.len, mod_text))
            continue;
        if (ch_pattern_match(mod->datamap_init_candidates[i], datadescinit_pattern))
            mod->datamap_init_candidates[mod->n_datamap_init_candidates++] =
                *(ch_ptr*)(mod->datamap_init_candidates[i] + 3);
    }

    ch_send_log_info(ctx,
                     "Found %lu candidates for DataMapInit functions in %s.",
                     mod->n_datamap_init_candidates,
                     ch_mod_names[mod_idx]);
}

ch_ptr ch_memmem(ch_ptr haystack, size_t haystack_len, ch_ptr needle, size_t needle_len)
{
    if (!haystack || !needle || haystack_len == 0 || needle_len == 0)
        return NULL;
    for (ch_ptr h = haystack; haystack_len >= needle_len; ++h, --haystack_len)
        if (!memcmp(h, needle, needle_len))
            return h;
    return NULL;
}

ch_ptr ch_memmem_cb(ch_ptr haystack,
                    size_t haystack_len,
                    ch_ptr needle,
                    size_t needle_len,
                    bool (*cb)(ch_ptr match, void* user_data),
                    void* user_data)
{
    while (needle_len <= haystack_len) {
        ch_ptr p = ch_memmem(haystack, haystack_len, needle, needle_len);
        if (!p)
            return NULL;
        if (cb(p, user_data))
            return p;
        size_t diff = CH_PTR_DIFF(p, haystack) + 1;
        haystack += diff;
        haystack_len -= diff;
    }
    return NULL;
}
