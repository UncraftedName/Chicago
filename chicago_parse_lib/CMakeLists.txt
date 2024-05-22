project(chicago_parse_lib)
set(CMAKE_C_STANDARD 23)

include_directories(
	src
	../shared
	../shared/thirdparty/msgpack/include
)

file(
	GLOB SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.c"
	"${PROJECT_SOURCE_DIR}/src/custom_restore/*.c"
	"${PROJECT_SOURCE_DIR}/src/custom_restore/registration/*.c"
	"${PROJECT_SOURCE_DIR}/src/block_handlers/*.c"
	"${PROJECT_SOURCE_DIR}/src/dump/*.c"
)

add_library(chicago_parse_lib STATIC ${SRC_FILES})
target_compile_definitions(chicago_parse_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
add_dependencies(chicago_parse_lib hashmap)
target_link_libraries(chicago_parse_lib PRIVATE hashmap msgpack)
