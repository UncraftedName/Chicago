#pragma once

#include "ch_byte_reader.h"
#include "ch_save.h"

ch_err ch_parse_save_from_reader(ch_parsed_save_data* parsed_data, ch_byte_reader* r);
ch_err ch_parse_save_header(ch_save_header* h, ch_byte_reader* r);
