project(chicago_payload)
set(CMAKE_C_STANDARD 23)

include_directories(
	src
	../chicago_parse_lib/src
)
file(GLOB SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.c")
add_library(chicago_payload SHARED ${SRC_FILES})
add_dependencies(chicago_payload chicago_parse_lib)
target_link_libraries(chicago_payload PRIVATE chicago_parse_lib)
