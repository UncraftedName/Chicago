#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <stdbool.h>

// TODO might be a good idea to move this file to the shared folder or something

#define CH_PIPE_NAME "\\\\.\\pipe\\chicago_pipe"

// TODO test these for really small sizes
#if 1
#define CH_PIPE_TIMEOUT_MS INFINITE
#else
#define CH_PIPE_TIMEOUT_MS 1000
#endif

// some datamaps are close to 100KB when sent over IPC in p1-5135
#define CH_PIPE_INIT_BUF_SIZE (1024 * 128)

typedef enum ch_game_module {
    // CH_MOD_CLIENT, // TODO FUCK FUCK FUCK FUCK
    CH_MOD_SERVER,
    CH_MOD_ENGINE,
    CH_MOD_VPHYSICS,
    CH_MOD_COUNT,
} ch_game_module;

static const char* const ch_mod_names[CH_MOD_COUNT] = {
    // "client.dll",
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
* {CH_MSG_TYPE: ch_comm_msg_type, CH_MSG_DATA: ...}
* 
* - For hello & goodbye, data is null.
* - For logging, data is a string.
* - For linked names, data is a key/value dict of associated name -> class name.
* 
* The only big one is datamaps. It's got the following data format (when sent over IPC):
* {
*   CH_MSG_DM_NAME:       str,
*   CH_MSG_DM_MODULE:     int, (ch_game_module)
*   CH_MSG_DM_MODULE_OFF: int, (offset into .dll)
*   CH_MSG_DM_BASE:       datamap|nil,
*   CH_MSG_DM_FIELDS:     list of type descriptions, len >= 0
* }
* Then each type description looks like this:
* {
*   CH_MSG_TD_NAME:           str,
*   CH_MSG_TD_TYPE:           ch_field_type,
*   CH_MSG_TD_FLAGS:          int,
*   CH_MSG_TD_EXTERNAL_NAME   str|nil,
*   CH_MSG_TD_OFF:            int,
*   CH_MSG_TD_TOTAL_SIZE:     int,
*   CH_MSG_TD_RESTORE_OPS:    int|nil, (offset into .dll)
*   CH_MSG_TD_INPUT_FUNC:     int|nil, (offset into .dll)
*   CH_MSG_TD_EMBEDDED:       datamap|nil,
*   CH_MSG_TD_OVERRIDE_COUNT: int,
*   CH_MSG_TD_TOL:            float,
* }
* 
* Each datamap is sent in full (all of the embedded & base maps are sent as well).
* The exe then verifies that datamaps are distinct, then saves the base/embedded
* in order to verify that all datamaps are distinct. When the datamaps are saved
* to disk, CH_MSG_DM_BASE & CH_MSG_TD_EMBEDDED fields are instead strings
* which uniquely reference a datamap that came before.
*/

#define CH_MSGPACK_KEYS_BEGIN(group_name) enum { _ch_##group_name##_group_start = __COUNTER__ }

#define CH_DEFINE_MSGPACK_KEY(group_name, ref_name, key_name)             \
    enum { ref_name = __COUNTER__ - _ch_##group_name##_group_start - 1 }; \
    static const char ref_name##_key[] = key_name

#define CH_MSGPACK_KEYS_END(group_name) \
    enum { _ch_##group_name##_group_end = __COUNTER__ - _ch_##group_name##_group_start - 1 }

#define CH_KEY_NAME(ref_name) ref_name##_key
#define CH_KEY_GROUP_COUNT(group_name) _ch_##group_name##_group_end

// update if any changes are made :)
#define CH_MSGPACK_FORMAT_VERSION 4

CH_MSGPACK_KEYS_BEGIN(KEYS_IPC_HEADER);
CH_DEFINE_MSGPACK_KEY(KEYS_IPC_HEADER, CH_IPC_TYPE, "msg_type");
CH_DEFINE_MSGPACK_KEY(KEYS_IPC_HEADER, CH_IPC_DATA, "msg_data");
CH_MSGPACK_KEYS_END(KEYS_IPC_HEADER);

CH_MSGPACK_KEYS_BEGIN(KEYS_FILE_HEADER);
CH_DEFINE_MSGPACK_KEY(KEYS_FILE_HEADER, CH_HEADER_VERSION, "format_version");
CH_DEFINE_MSGPACK_KEY(KEYS_FILE_HEADER, CH_HEADER_GAME_NAME, "game_name");
CH_DEFINE_MSGPACK_KEY(KEYS_FILE_HEADER, CH_HEADER_GAME_VERSION, "game_version");
CH_DEFINE_MSGPACK_KEY(KEYS_FILE_HEADER, CH_HEADER_DATAMAPS, "datamaps");
CH_DEFINE_MSGPACK_KEY(KEYS_FILE_HEADER, CH_HEADER_LINKED_NAMES, "linked_names");
CH_MSGPACK_KEYS_END(KEYS_FILE_HEADER);

// TODO chains_validated, packed_offsets_computed, packed_size
CH_MSGPACK_KEYS_BEGIN(KEYS_DM);
CH_DEFINE_MSGPACK_KEY(KEYS_DM, CH_DM_NAME, "name");
CH_DEFINE_MSGPACK_KEY(KEYS_DM, CH_DM_MODULE, "module");
CH_DEFINE_MSGPACK_KEY(KEYS_DM, CH_DM_MODULE_OFF, "module_offset");
CH_DEFINE_MSGPACK_KEY(KEYS_DM, CH_DM_BASE, "base_map");
CH_DEFINE_MSGPACK_KEY(KEYS_DM, CH_DM_FIELDS, "fields");
CH_MSGPACK_KEYS_END(KEYS_DM);

// TODO override_field
CH_MSGPACK_KEYS_BEGIN(KEYS_TD);
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_NAME, "name");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_TYPE, "type");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_FLAGS, "flags");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_EXTERNAL_NAME, "external_name");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_OFF, "offset");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_NUM_ELEMS, "n_elems");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_TOTAL_SIZE, "total_size_bytes");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_RESTORE_OPS, "restore_ops");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_INPUT_FUNC, "input_func");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_EMBEDDED, "embedded_map");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_OVERRIDE_COUNT, "override_count");
CH_DEFINE_MSGPACK_KEY(KEYS_TD, CH_TD_TOL, "float_tolerance");
CH_MSGPACK_KEYS_END(KEYS_TD);
