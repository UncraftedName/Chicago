project(chicago_parser)
set(CMAKE_C_STANDARD 23)

include_directories(
	src
	../chicago_parse_lib/src
	../chicago_payload/src
)
file(GLOB SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.c"
	"${PROJECT_SOURCE_DIR}/../chicago_payload/src/ch_payload_comm.c"
)
add_executable(chicago_parser ${SRC_FILES})
add_dependencies(chicago_parser chicago_parse_lib)
target_link_libraries(chicago_parser PRIVATE chicago_parse_lib)
