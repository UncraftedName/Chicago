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

Use this for datamap lookup? https://github.com/fanf2/qp
*/

#include "ch_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ch_search.h"

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
datamap_t* ch_find_datamap_by_name(const ch_mod_info* module_info, const char* name)
{
    size_t name_len = strlen(name);
    const char* sec_data = module_info->sections[CH_SEC_RDATA].start;
    size_t sec_size = module_info->sections[CH_SEC_RDATA].len;
    const void* match = ch_memmem(sec_data, sec_size, name, name_len + 1);
    (void)match;
    int x = 1;
    (void)x;
    return NULL;
}
