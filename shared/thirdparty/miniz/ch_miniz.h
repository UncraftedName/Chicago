#pragma once

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES

#pragma warning(push)
#pragma warning(disable : 4820)
#include "miniz.h"
#pragma warning(pop)
