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

bool ch_pattern_match(ch_ptr mem, ch_mod_sec mod_sec_text, ch_pattern pattern)
{
    if (!ch_ptr_in_sec(mem, mod_sec_text, pattern.len))
        return false;
    for (size_t i = 0; i < pattern.len; i++)
        if (pattern.bytes[i] != mem[i] && !(pattern.wildmask[i / 8] & (1 << (i & 7))))
            return false;
    return true;
}

int ch_pattern_multi_match(ch_ptr mem, ch_mod_sec mod_sec_text, ch_pattern* patterns, size_t n_patterns)
{
    for (int i = 0; i < (int)n_patterns; i++)
        if (ch_pattern_match(mem, mod_sec_text, patterns[i]))
            return i;
    return CH_MULTI_PATTERN_NOT_FOUND;
}

int ch_pattern_multi_search(ch_ptr* found_mem,
                            ch_mod_sec mod_sec_text,
                            ch_pattern* patterns,
                            size_t n_patterns,
                            bool ensure_unique)
{
    if (found_mem)
        *found_mem = NULL;
    for (ch_ptr mem = mod_sec_text.start; mem < mod_sec_text.start + mod_sec_text.len; mem++) {
        int i = ch_pattern_multi_match(mem, mod_sec_text, patterns, n_patterns);
        if (i != CH_MULTI_PATTERN_NOT_FOUND) {
            if (ensure_unique && mem + 1 < mod_sec_text.start + mod_sec_text.len) {
                ch_mod_sec dummy_sec = {
                    .start = mem + 1,
                    .len = CH_PTR_DIFF(mod_sec_text.start + mod_sec_text.len, mem + 1),
                };
                if (ch_pattern_multi_search(NULL, dummy_sec, patterns, n_patterns, false) !=
                    CH_MULTI_PATTERN_NOT_FOUND) {
                    return CH_MULTI_PATTERN_DUP;
                }
            }
            if (found_mem)
                *found_mem = mem;
            return i;
        }
    }
    return CH_MULTI_PATTERN_NOT_FOUND;
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
    if (!ch_pattern_match(info->cvar_ctor_out, info->server_mod_text, info->ctor_pattern))
        return false;
    if (*(ch_ptr*)(info->cvar_ctor_out + 5) != info->cvar_desc_str)
        return false;
    info->cvar_impl_out = *(ch_ptr*)(info->cvar_ctor_out + 10);
    if (!ch_ptr_in_sec(info->cvar_impl_out, info->server_mod_text, 0))
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

void ch_find_static_inits(ch_send_ctx* ctx, ch_search_ctx* sc)
{
    // we're using patterns, I don't care :)
    struct ch_pattern_info {
        const char* str;
        const char* name;
        unsigned char scratch[64]; // big enough to hold the biggest one
        size_t offset_to_static_inits_start_in_pattern;
        size_t offset_to_static_inits_end_in_pattern;
    } patterns_infos[] = {
        {
            .str = "BE ?? ?? ?? ?? 8B C6 BF ?? ?? ?? ?? 3B C7 59 73 0F 8B 06 85 C0 74 02 FF D0 83 C6 04 3B F7 72 F1",
            .name = "vs2005",
            .offset_to_static_inits_start_in_pattern = 1,
            .offset_to_static_inits_end_in_pattern = 8,
        },
        {
            .str = "E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 59 59 85 C0 75 ?? 68 ?? ?? ?? ?? E8 ?? "
                   "?? ?? ?? C7 04 24 ?? ?? ?? ?? 68 ?? ?? ?? ?? E8 ?? ?? ?? ??",
            .name = "vs2008/2010/2012",
            .offset_to_static_inits_start_in_pattern = 44,
            .offset_to_static_inits_end_in_pattern = 39,
        },
    };

    ch_pattern patterns[ARRAYSIZE(patterns_infos)];

    for (size_t i = 0; i < ARRAYSIZE(patterns); i++)
        ch_parse_pattern_str(patterns_infos[i].str, &patterns[i], patterns_infos[i].scratch);

    for (size_t i = 0; i < CH_MOD_COUNT; i++) {

        ch_ptr mod_base = sc->mods[i].base;
        const char* mod_name = ch_mod_names[i];
        ch_mod_sec sec_text = sc->mods[i].sections[CH_SEC_TEXT];
        ch_mod_sec sec_rdata = sc->mods[i].sections[CH_SEC_RDATA];

        ch_ptr mem;
        int search_idx = ch_pattern_multi_search(&mem, sec_text, patterns, ARRAYSIZE(patterns), true);
        if (search_idx < 0) {
            CH_PAYLOAD_LOG_ERR(ctx,
                               "failed to find static init caller in %s%s",
                               mod_name,
                               search_idx == CH_MULTI_PATTERN_NOT_FOUND ? "" : " (multiple matches)");
        }
        struct ch_pattern_info* info = &patterns_infos[search_idx];

        CH_PAYLOAD_LOG_INFO(ctx,
                            "found static init caller at %s[0x%08X] using the '%s' pattern",
                            mod_name,
                            CH_PTR_DIFF(mem, mod_base),
                            info->name);

        ch_ptr* static_inits_start = *(ch_ptr**)(mem + info->offset_to_static_inits_start_in_pattern);
        ch_ptr* static_inits_end = *(ch_ptr**)(mem + info->offset_to_static_inits_end_in_pattern);

        if (!ch_ptr_in_sec((ch_ptr)static_inits_start, sec_rdata, sizeof(ch_ptr)) ||
            !ch_ptr_in_sec((ch_ptr)static_inits_end, sec_rdata, sizeof(ch_ptr))) {
            CH_PAYLOAD_LOG_ERR(ctx, "bogus static init pointers found in '%s'", mod_name);
        }

        sc->mods[i].static_inits = static_inits_start;
        sc->mods[i].n_static_inits = static_inits_start > static_inits_end
                                       ? 0
                                       : CH_PTR_DIFF(static_inits_end, static_inits_start) / sizeof(ch_ptr);

        CH_PAYLOAD_LOG_INFO(ctx,
                            "found %d static init functions at %s[0x%08X]",
                            (int)sc->mods[i].n_static_inits,
                            mod_name,
                            CH_PTR_DIFF(sc->mods[i].static_inits, mod_base));
    }
}

bool ch_datamap_looks_valid(const datamap_t* dm, const ch_mod_info* mod)
{
    ch_mod_sec mod_data = mod->sections[CH_SEC_DATA];
    ch_mod_sec mod_rdata = mod->sections[CH_SEC_RDATA];
    ch_mod_sec mod_text = mod->sections[CH_SEC_TEXT];

    if (!ch_ptr_in_sec((ch_ptr)dm, mod_data, sizeof *dm))
        return false;
    if (!dm->dataClassName || !dm->dataDesc || dm->dataNumFields <= 0 || dm->packed_size < 0)
        return false;
    if (!ch_ptr_in_sec((ch_ptr)dm->dataDesc, mod_data, sizeof(*dm->dataDesc) * dm->dataNumFields))
        return false;
    if (!ch_str_in_sec(dm->dataClassName, mod_rdata) || !*dm->dataClassName)
        return false;
    // "empty" datadescs are allowed, they just have a single empty zeroed field
    typedescription_t empty_desc = {0};
    if (dm->dataNumFields > 1 || memcmp(&empty_desc, dm->dataDesc, sizeof(typedescription_t))) {
        // iterate type description
        for (int i = 0; i < dm->dataNumFields; i++) {
            const typedescription_t* desc = &dm->dataDesc[i];
            if (desc->fieldType < FIELD_VOID || desc->fieldType >= FIELD_TYPECOUNT || !desc->fieldName)
                return false;
            if (desc->fieldSizeInBytes < 0 || desc->fieldOffset[TD_OFFSET_NORMAL] < 0)
                return false;
            if (!ch_str_in_sec(desc->fieldName, mod_rdata)) {
                if (desc->fieldType != FIELD_VOID || !desc->inputFunc || desc->flags != FTYPEDESC_FUNCTIONTABLE)
                    return false;
                // game typedescs added with DEFINE_FUNCTION_RAW will allocate the field name with new for some stupid reason
                __try {
                    volatile size_t a = strlen(desc->fieldName);
                    (void)a;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    return false;
                }
            }
            if (desc->externalName && !ch_str_in_sec(desc->externalName, mod_rdata))
                return false;
            if (desc->pSaveRestoreOps && !ch_ptr_in_sec((ch_ptr)desc->pSaveRestoreOps, mod_data, 0))
                return false;
            if (desc->inputFunc && !ch_ptr_in_sec((ch_ptr)desc->inputFunc, mod_text, 0))
                return false;
            if (desc->td && !ch_datamap_looks_valid(desc->td, mod))
                return false;
        }
    }
    // could optimize the recursive calls if we need to, a lot of datamaps inherit from others...
    return !dm->baseMap || ch_datamap_looks_valid(dm->baseMap, mod);
}

void ch_iterate_datamaps(ch_send_ctx* ctx,
                         ch_search_ctx* sc,
                         ch_game_module mod_idx,
                         void (*cb)(const datamap_t* dm, void* user_data),
                         void* user_data)
{
    /*
    * The static init table has functions of the type (void*)(void), but the DataDescInit functions
    * call the DataMapInit functions with a null param. This is great, because it means the DataMapInit
    * function is not inlined (at least from what I've seen) and has a very consistent and simple
    * pattern. The pattern is just:
    * 
    * push 0
    * call [datamapinit]
    * add esp, 4
    * mov [datamap], eax
    * ret
    * 
    * So we filter through the static inits and find which ones match this pattern. These are of
    * course not guaranteed to be DataMapInit functions, but they narrow down the search a lot. Then
    * we can just check if the [datamap] points to a pointer of a valid datamap :).
    */

    const char* pattern_str = "6A 00 E8 ?? ?? ?? ?? 83 C4 04 A3 ?? ?? ?? ?? C3";
    unsigned char pattern_scratch[18];
    ch_pattern datadescinit_pattern;
    ch_parse_pattern_str(pattern_str, &datadescinit_pattern, pattern_scratch);

    bool any = false;
    ch_mod_info* mod = &sc->mods[mod_idx];
    for (size_t i = 0; i < mod->n_static_inits; i++) {
        if (!ch_pattern_match(mod->static_inits[i], mod->sections[CH_SEC_TEXT], datadescinit_pattern))
            continue;
        const datamap_t* const* dm = *(const datamap_t***)(mod->static_inits[i] + 11);
        if (!ch_ptr_in_sec((ch_ptr)dm, mod->sections[CH_SEC_DATA], sizeof(ch_ptr)))
            continue;
        if (!ch_datamap_looks_valid(*dm, mod))
            continue;
        cb(*dm, user_data);
        any = true;
    }
    if (!any)
        ch_send_err_and_exit(ctx, "Could not find any datamaps in %s.", ch_mod_names[mod_idx]);
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
