#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <stdbool.h>

#define CH_PIPE_NAME "\\\\.\\pipe\\chicago_pipe"

// TODO test these for really small sizes
#if 1
#define CH_PIPE_TIMEOUT_MS INFINITE
#else
#define CH_PIPE_TIMEOUT_MS 1000
#endif

#define CH_PIPE_INIT_BUF_SIZE (1024 * 4)

typedef enum ch_game_module {
    CH_MOD_CLIENT,
    CH_MOD_SERVER,
    CH_MOD_ENGINE,
    CH_MOD_VPHYSICS,
    CH_MOD_COUNT,
} ch_game_module;

static const char* const ch_mod_names[CH_MOD_COUNT] = {
    "client.dll",
    "server.dll",
    "engine.dll",
    "vphysics.dll",
};

typedef enum ch_comm_msg_type {
    // first message
    CH_MSG_HELLO,
    // sending a whole-ass datamap!
    CH_MSG_DATAMAP,
    // sending an associated name of a datamap (e.g. prop_physics is actually a CPropPhysics)
    CH_MSG_LINKED_NAME,
    // for cmd printing, general info
    CH_MSG_LOG_INFO,
    // for cmd printing, we ran into some unrecoverable error and are stopping
    CH_MSG_LOG_ERROR,
    // we're done and all was successful
    CH_MSG_GOODBYE,
} ch_comm_msg_type;

bool ch_get_required_modules(DWORD proc_id, BYTE* base_addresses[CH_MOD_COUNT]);

/*
* All msgpack data sent from the payload is meant to be somewhat human readable
* if you were to print it out. It has the following format:
* {MSG_TYPE: ch_comm_msg_type, MSG_DATA: ...}
* 
* - For hello & goodbye, data is null.
* - For logging, data is a string.
* - For linked names, data is a key/value dict of associated name -> class name.
* 
* The only big one is datamaps.
*/
#define CHMPK_MSG_TYPE "msg_type"
#define CHMPK_MSG_DATA "msg_data"
