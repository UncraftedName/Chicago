project(chicago_payload)
set(CMAKE_C_STANDARD 23)

if (MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /EHa")
endif()

include_directories(
	src
	../chicago_parse_lib/src
	../shared
	../shared/thirdparty/msgpack/include
)

file(GLOB SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.c")
add_library(chicago_payload SHARED ${SRC_FILES})
add_dependencies(chicago_payload chicago_parse_lib msgpack)
target_link_libraries(chicago_payload PRIVATE chicago_parse_lib msgpack x86)
