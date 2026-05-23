cmake_minimum_required(VERSION 3.16)
project(sort_visualizer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(GMODULE REQUIRED gmodule-2.0)

set(PLATFORM_DL_LIB)
if(NOT WIN32)
    set(PLATFORM_DL_LIB dl)
endif()

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
    ${PLATFORM_DL_LIB}
)

if(NOT WIN32)
target_link_libraries(sort-visualizer-worker PRIVATE
    ${GTK3_LIBRARIES}
    ${GMODULE_LIBRARIES}
    ${PLATFORM_DL_LIB}
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

if(BUILD_TESTING)
add_executable(test-sort-unit
    tests/unit_sort_engine_test.c
    src/sort_engine.c
)

target_include_directories(test-sort-unit PRIVATE
    include
    ${GTK3_INCLUDE_DIRS}
    ${GMODULE_INCLUDE_DIRS}
)

target_compile_options(test-sort-unit PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    ${GTK3_CFLAGS_OTHER}
    ${GMODULE_CFLAGS_OTHER}
)

target_link_libraries(test-sort-unit PRIVATE
    ${GTK3_LIBRARIES}
    ${GMODULE_LIBRARIES}
    ${PLATFORM_DL_LIB}
)

target_link_directories(test-sort-unit PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_directories(test-sort-unit PRIVATE ${GMODULE_LIBRARY_DIRS})
target_link_options(test-sort-unit PRIVATE ${GTK3_LDFLAGS_OTHER} ${GMODULE_LDFLAGS_OTHER})

add_executable(test-sort-integration
    tests/integration_custom_sort_test.c
    src/sort_engine.c
)

target_include_directories(test-sort-integration PRIVATE
    include
    ${GTK3_INCLUDE_DIRS}
    ${GMODULE_INCLUDE_DIRS}
)

target_compile_options(test-sort-integration PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    ${GTK3_CFLAGS_OTHER}
    ${GMODULE_CFLAGS_OTHER}
)

target_link_libraries(test-sort-integration PRIVATE
    ${GTK3_LIBRARIES}
    ${GMODULE_LIBRARIES}
    ${PLATFORM_DL_LIB}
)

target_link_directories(test-sort-integration PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_directories(test-sort-integration PRIVATE ${GMODULE_LIBRARY_DIRS})
target_link_options(test-sort-integration PRIVATE ${GTK3_LDFLAGS_OTHER} ${GMODULE_LDFLAGS_OTHER})

if(NOT WIN32)
    add_dependencies(test-sort-integration sort-visualizer-worker)
endif()

add_test(NAME unit-sort-engine COMMAND test-sort-unit)
add_test(NAME integration-custom-sort COMMAND test-sort-integration)
endif()
