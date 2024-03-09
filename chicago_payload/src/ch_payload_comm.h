#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <libloaderapi.h>
#include <processthreadsapi.h>
#pragma warning(push, 3)
#include <DbgHelp.h>
#pragma warning(pop)

#include "stdbool.h"

#define CH_PIPE_NAME "\\\\.\\pipe\\chicago_pipe"

enum ch_game_module {
    CH_MOD_CLIENT,
    CH_MOD_SERVER,
    CH_MOD_ENGINE,
    CH_MOD_VPHYSICS,
    CH_MOD_COUNT,
};

static const wchar_t* const ch_required_module_names[CH_MOD_COUNT] = {
    L"client.dll",
    L"server.dll",
    L"engine.dll",
    L"vphysics.dll",
};

enum ch_mod_section {
    CH_SEC_TEXT,
    CH_SEC_DATA,
    CH_SEC_RDATA,
    CH_SEC_COUNT,
};

static const char* const ch_mod_sec_names[CH_SEC_COUNT] = {
    ".text",
    ".data",
    ".rdata",
};

typedef struct ch_mod_info {
    struct {
        void* start;
        size_t len;
    } sections[CH_SEC_COUNT];
} ch_mod_info;

bool ch_get_module_info(DWORD proc_id, ch_mod_info info[CH_MOD_COUNT]);

typedef enum ch_pipe_message_type {
    CH_MSG_DATAMAP,
    CH_MSG_LINKED_NAME,
    CH_MSG_DONE,
} ch_pipe_message_type;
