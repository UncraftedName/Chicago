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

// some datamaps are close to 100MB went sent over IPC in p1-5135
#define CH_PIPE_INIT_BUF_SIZE (1024 * 128)

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
* if you were to print it out or debug it. It has the following format:
* {CHMPK_MSG_TYPE: ch_comm_msg_type, CHMPK_MSG_DATA: ...}
* 
* - For hello & goodbye, data is null.
* - For logging, data is a string.
* - For linked names, data is a key/value dict of associated name -> class name.
* 
* The only big one is datamaps. It's got the following data format (when sent over IPC):
* {
*   CHMPK_MSG_DM_NAME:       str,
*   CHMPK_MSG_DM_MODULE:     int, (ch_game_module)
*   CHMPK_MSG_DM_MODULE_OFF: int, (offset into .dll)
*   CHMPK_MSG_DM_BASE:       datamap|nil,
*   CHMPK_MSG_DM_FIELDS:     list of type descriptions, len >= 0
* }
* Then each type description looks like this:
* {
*   CHMPK_MSG_TD_NAME:           str,
*   CHMPK_MSG_TD_TYPE:           ch_field_type,
*   CHMPK_MSG_TD_FLAGS:          int,
*   CHMPK_MSG_TD_OFF:            int,
*   CHMPK_MSG_TD_TOTAL_SIZE:     int,
*   CHMPK_MSG_TD_RESTORE_OPS:    int|nil, (offset into .dll)
*   CHMPK_MSG_TD_INPUT_FUNC:     int|nil, (offset into .dll)
*   CHMPK_MSG_TD_EMBEDDED:       datamap|nil,
*   CHMPK_MSG_TD_OVERRIDE_COUNT: int,
*   CHMPK_MSG_TD_TOL:            float,
* }
* 
* Each datamap is sent in full (all of the embedded & base maps are sent as well).
* The exe then verifies that datamaps are distinct, then saves the base/embedded
* in order to verify that all datamaps are distinct. When the datamaps are saved
* to disk, CHMPK_MSG_DM_BASE & CHMPK_MSG_TD_EMBEDDED fields are instead strings
* which uniquely reference a datamap that came before.
*/
#define CHMPK_MSG_TYPE "msg_type"
#define CHMPK_MSG_DATA "msg_data"

#define CHMPK_MSG_DM_NAME "name"
#define CHMPK_MSG_DM_MODULE "module"
#define CHMPK_MSG_DM_MODULE_OFF "module_offset"
#define CHMPK_MSG_DM_BASE "base_map"
#define CHMPK_MSG_DM_FIELDS "fields"
// TODO chains_validated, packed_offsets_computed, packed_size

#define CHMPK_MSG_TD_NAME "name"
#define CHMPK_MSG_TD_TYPE "type"
#define CHMPK_MSG_TD_FLAGS "flags"
#define CHMPK_MSG_TD_OFF "offset"
#define CHMPK_MSG_TD_TOTAL_SIZE "total_size_bytes"
#define CHMPK_MSG_TD_RESTORE_OPS "restore_ops"
#define CHMPK_MSG_TD_INPUT_FUNC "input_func"
#define CHMPK_MSG_TD_EMBEDDED "embedded_map"
#define CHMPK_MSG_TD_OVERRIDE_COUNT "override_count"
#define CHMPK_MSG_TD_TOL "float_tolerance"
// TODO override_field
