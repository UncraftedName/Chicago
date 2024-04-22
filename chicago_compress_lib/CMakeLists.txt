project(chicago_compress_lib)
set(CMAKE_C_STANDARD 23)

include_directories(
	src
	../chicago_parse_lib/src
	../chicago_payload/src
	../shared
	../shared/thirdparty/msgpack/include
	../shared/thirdparty/brotli
	../shared/thirdparty/brotli/include
)

file(GLOB SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.c"
	"${PROJECT_SOURCE_DIR}/../chicago_parse_lib/src"
)

add_library(chicago_compress_lib STATIC ${SRC_FILES})
add_dependencies(chicago_compress_lib chicago_parse_lib hashmap brotli miniz)
target_link_libraries(chicago_compress_lib PRIVATE chicago_parse_lib hashmap brotli miniz)
