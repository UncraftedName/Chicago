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
bool ch_pattern_match(const unsigned char* mem, ch_pattern pattern)
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

const void* ch_memmem(const void* restrict haystack,
                      size_t haystack_len,
                      const void* restrict needle,
                      size_t needle_len)
{
    if (!haystack || !needle || haystack_len == 0 || needle_len == 0)
        return NULL;
    for (const char* h = haystack; haystack_len >= needle_len; ++h, --haystack_len)
        if (!memcmp(h, needle, needle_len))
            return h;
    return NULL;
}

/*
* 1) find the string corresponding to the name of the datamap
* 2) find instructions that reference that string (TODO consider only searching instructions corresponding to static initializers?)
* 3) determine the address of the datamap from that function
* 4) verify that the datamap is valid
*/
// this is by no means the "best", "most correct", "easiest", or the fastest way to do this
datamap_t* ch_find_datamap_by_name(const ch_mod_info* module_info, const char* name)
{
    size_t name_len = strlen(name);
    const char* sec_data = module_info->sections[CH_SEC_RDATA].start;
    size_t sec_size = module_info->sections[CH_SEC_RDATA].len;
    // find the string
    const void* str_match = ch_memmem(sec_data, sec_size, name, name_len + 1);
    // find an instruction which uses the string
    const void* match = ch_memmem(module_info->sections[CH_SEC_TEXT].start,
                                  module_info->sections[CH_SEC_TEXT].len,
                                  &str_match,
                                  sizeof str_match);
    (void)match;
    int x = 1;
    (void)x;
    return NULL;
}
