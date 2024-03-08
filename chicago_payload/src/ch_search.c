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

#pragma warning(push, 3)
// #include "function_length.h"
#pragma warning(pop)

ch_err ch_search_pe_file(ch_search_result* result)
{
    (void)result;

    // FILE* f = fopen("G:/Games/portal/Portal Source/portal/bin/server.dylib", "rb");
    // const char* test_path = "G:/Games/portal/Portal Source/portal/bin/server.dll";
    // FILE* f = fopen(test_path, "rb");
    // fseek(f, 0, SEEK_END);
    // long int size = ftell(f);
    // rewind(f);
    // char* bytes = malloc(size);
    // fread(bytes, 1, size, f);
    // fclose(f);

    // size_t off = 0xe2b20; // 5135 server.dll   CBaseEntity
    // size_t off = 0x39b50; // 5135 server.dylib CBaseEntity

    // pFunctionInfo info = getFunctionLength(bytes + off, X86);
    // (void)info;

    // const char* search_name = "CBaseEntity";
    // char sn[64];
    // sn[0] = '\0';
    // strcpy(sn + 1, search_name);

    return CH_ERR_NONE;
}

bool ch_proc_has_required_modules(DWORD proc_id)
{
    HANDLE snap = (HANDLE)ERROR_BAD_LENGTH;
    for (int i = 0; i < 10 && snap == (HANDLE)ERROR_BAD_LENGTH; i++)
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, proc_id);
    if (snap == (HANDLE)ERROR_BAD_LENGTH || snap == INVALID_HANDLE_VALUE)
        return false;

    struct {
        WCHAR* name;
        unsigned int flag;
    } required_mods[] = {
        {L"client.dll", 1},
        {L"server.dll", 2},
        {L"engine.dll", 4},
        {L"vphysics.dll", 8},
    };

    const int num_required_mods = sizeof(required_mods) / sizeof(*required_mods);
    unsigned int mod_flags = 0, target_flags = 0;
    for (int i = 0; i < num_required_mods; i++)
        target_flags |= required_mods[i].flag;

    MODULEENTRY32W me32w = {.dwSize = sizeof(MODULEENTRY32W)};
    if (Module32FirstW(snap, &me32w)) {
        do {
            for (int j = 0; j < num_required_mods; j++) {
                if (!wcscmp(required_mods[j].name, me32w.szModule)) {
                    mod_flags |= required_mods[j].flag;
                    break;
                }
            }
        } while (mod_flags != target_flags && Module32NextW(snap, &me32w));
    }
    CloseHandle(snap);
    return mod_flags == target_flags;
}
