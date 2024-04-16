project(hashmap)
set(CMAKE_C_STANDARD 23)

if (MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

file(GLOB SRC_FILES "${PROJECT_SOURCE_DIR}/hashmap.c")
add_library(hashmap STATIC ${SRC_FILES})
