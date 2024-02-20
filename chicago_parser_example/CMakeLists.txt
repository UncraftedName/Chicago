project(chicago_parser_example)
set(CMAKE_C_STANDARD 23)

include_directories(
	../chicago_parser_lib/src
	src
)
file(GLOB SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.c")
add_executable(chicago_parser_example ${SRC_FILES})
add_dependencies(chicago_parser_example  chicago_parser_lib)
target_link_libraries(chicago_parser_lib)
