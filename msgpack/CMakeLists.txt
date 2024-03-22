project(msgpack)
set(CMAKE_C_STANDARD 23)

if (MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Wall /wd4820 /wd4774")
endif()

include_directories(
	src
	include
	include/msgpack
)
file(GLOB SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.c")
add_library(msgpack STATIC ${SRC_FILES})
