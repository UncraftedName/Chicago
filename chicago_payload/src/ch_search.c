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

// DataDescInit pattern: 6A 00 E8 ?? ?? ?? ?? 83 C4 04 A3 ?? ?? ?? ?? C3 (needs 18 bytes of scratch space)
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

void ch_find_entity_factory_cvar(ch_send_ctx* ctx, ch_search_ctx* sc)
{
    ch_mod_info* server_mod = &sc->mods[CH_MOD_SERVER];
    ch_mod_sec server_rdata = server_mod->sections[CH_SEC_RDATA];
    ch_mod_sec server_text = server_mod->sections[CH_SEC_TEXT];

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
        if (!str_infos[i].p)
            ch_send_err_and_exit(ctx,
                                 "Failed to find string '%s' in the .rdata section in server.dll.",
                                 str_infos[i].str);
        if (str_infos[i].p == (void*)(-1))
            ch_send_err_and_exit(ctx,
                                 "Found multiple of string '%s' in the .rdata section in server.dll (expected 1).",
                                 str_infos[i].str);
    }

    /*
    * Find the ctor of the dumpentityfactories cvar. This is done by looking for the first location where the
    * address to the above strings is referenced within some small number of instructions from each other and
    * the pattern below is matched.
    */

    const size_t str_search_threshold = 128;

    // we need the function start & the cvar callback from this ctor, with a pattern we can make sure we get both
    const char* ctor_pattern_str = "6A 00 6A 04 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? B9 ?? ?? ?? ??";
    unsigned char ctor_pattern_scratch[34];
    ch_pattern ctor_pattern;
    ch_parse_pattern_str(ctor_pattern_str, &ctor_pattern, ctor_pattern_scratch);

    ch_ptr search_start = server_text.start;
    size_t search_len = server_text.len;
    ch_ptr search_end = search_start + server_text.len;
    while (search_start < search_end) {
        // find instructions which have the same bytes as the address of the first string
        ch_ptr str1_ref = ch_memmem(search_start, search_len, (ch_ptr)&str_infos[0].p, sizeof str_infos[0].p);
        if (!str1_ref)
            break;

        // within some threshold around the first match, find instructions which have the same bytes as the address of the second string
        ch_ptr search_start2 = max(server_text.start, str1_ref - str_search_threshold);
        ch_ptr search_end2 = min(search_end, str1_ref + str_search_threshold);
        ch_ptr str2_ref = ch_memmem(search_start2,
                                    CH_PTR_DIFF(search_end2, search_start2),
                                    (ch_ptr)&str_infos[1].p,
                                    sizeof str_infos[1].p);

        if (str2_ref) {
            // this is likely the ctor, the pattern will tell us for sure and tell us if our offset of 15 is correct
            sc->dump_entity_factories_ctor = str1_ref - 15;
            if (ch_pattern_match(sc->dump_entity_factories_ctor, ctor_pattern)) {
                sc->dump_entity_factories_impl = *(ch_ptr*)(sc->dump_entity_factories_ctor + 11);
                break;
            }
            ch_send_log_info(ctx,
                             "Found a potential candidate for 'dumpentityfactories' ConCommand ctor at "
                             "server.dll+0x%08X, but it didn't match the pattern.",
                             CH_PTR_DIFF(str1_ref, server_mod->base));
            sc->dump_entity_factories_ctor = NULL;
        }

        size_t diff = CH_PTR_DIFF(str1_ref, search_start) + 1;
        search_start += diff;
        search_len -= diff;
    }
    if (!sc->dump_entity_factories_ctor)
        ch_send_err_and_exit(ctx, "Found no candidates for 'dumpentityfactories' ConCommand ctor.");

    ch_send_log_info(ctx,
                     "Found 'dumpentityfactories' ConCommand ctor at server.dll+0x%08X",
                     CH_PTR_DIFF(sc->dump_entity_factories_ctor, server_mod->base));
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
