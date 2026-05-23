cmake_minimum_required(VERSION 3.16)
project(sort_visualizer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(GMODULE REQUIRED gmodule-2.0)

add_executable(sort-visualizer
    src/main.c
    src/sort_engine.c
)

if(NOT WIN32)
add_executable(sort-visualizer-worker
    src/custom_worker.c
)
endif()

target_include_directories(sort-visualizer PRIVATE
    include
    ${GTK3_INCLUDE_DIRS}
    ${GMODULE_INCLUDE_DIRS}
)

target_compile_options(sort-visualizer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    ${GTK3_CFLAGS_OTHER}
    ${GMODULE_CFLAGS_OTHER}
)

target_link_libraries(sort-visualizer PRIVATE
    ${GTK3_LIBRARIES}
    ${GMODULE_LIBRARIES}
    dl
)

if(NOT WIN32)
target_link_libraries(sort-visualizer-worker PRIVATE
    ${GTK3_LIBRARIES}
    ${GMODULE_LIBRARIES}
    dl
)
endif()

target_link_directories(sort-visualizer PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_directories(sort-visualizer PRIVATE ${GMODULE_LIBRARY_DIRS})
target_link_options(sort-visualizer PRIVATE ${GTK3_LDFLAGS_OTHER} ${GMODULE_LDFLAGS_OTHER})

if(NOT WIN32)
target_include_directories(sort-visualizer-worker PRIVATE
    include
    ${GTK3_INCLUDE_DIRS}
    ${GMODULE_INCLUDE_DIRS}
)

target_compile_options(sort-visualizer-worker PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    ${GTK3_CFLAGS_OTHER}
    ${GMODULE_CFLAGS_OTHER}
)

target_link_directories(sort-visualizer-worker PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_directories(sort-visualizer-worker PRIVATE ${GMODULE_LIBRARY_DIRS})
target_link_options(sort-visualizer-worker PRIVATE ${GTK3_LDFLAGS_OTHER} ${GMODULE_LDFLAGS_OTHER})
endif()
