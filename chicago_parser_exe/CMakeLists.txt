project(chicago)
set(CMAKE_C_STANDARD 23)

include_directories(
	src
	../chicago_parse_lib/src
	../chicago_payload/src
	../chicago_compress_lib/src
	../chicago_payload/src
	../shared
	../shared/thirdparty/msgpack/include
	../shared/thirdparty/brotli
	../shared/thirdparty/brotli/include
)

file(GLOB SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.c"
	"${PROJECT_SOURCE_DIR}/../chicago_payload/src/ch_payload_comm_shared.c"
)

add_executable(chicago ${SRC_FILES})
add_dependencies(chicago chicago_parse_lib msgpack hashmap brotli miniz chicago_compress_lib)
target_link_libraries(chicago PRIVATE chicago_parse_lib msgpack hashmap brotli miniz chicago_compress_lib)
