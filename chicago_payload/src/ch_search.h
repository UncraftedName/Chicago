#pragma once

#include "ch_payload_comm.h"
#include "ch_save.h"

// looks for a datamap in the .data section by the datamap name
datamap_t* ch_find_datamap_by_name(const ch_mod_info* module_info, const char* name);

const void* ch_memmem(const void* restrict haystack,
                      size_t haystack_len,
                      const void* restrict needle,
                      size_t needle_len);
