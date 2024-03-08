#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <libloaderapi.h>
#include <processthreadsapi.h>

#include "ch_save.h"

typedef struct ch_search_result {
    int n_datamaps;
} ch_search_result;

// returns true if the given process has client, server, engine, & vphysics dlls
bool ch_proc_has_required_modules(DWORD proc_id);

ch_err ch_search_pe_file(ch_search_result* result);
