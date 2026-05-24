cmake_minimum_required(VERSION 3.16)
project(sort_visualizer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(ENABLE_COVERAGE "Enable line coverage instrumentation" OFF)

include(CTest)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(GMODULE REQUIRED gmodule-2.0)

set(PLATFORM_DL_LIB)
if(NOT WIN32)
    set(PLATFORM_DL_LIB dl)
endif()

set(COVERAGE_COMPILE_OPTIONS)
set(COVERAGE_LINK_OPTIONS)

if(ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set(COVERAGE_COMPILE_OPTIONS -O0 -g --coverage)
        set(COVERAGE_LINK_OPTIONS --coverage)
        message(STATUS "Coverage instrumentation enabled")
    else()
        message(FATAL_ERROR "ENABLE_COVERAGE is only supported with GCC or Clang")
    endif()
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

install(TARGETS sort-visualizer RUNTIME DESTINATION bin)
if(NOT WIN32)
install(TARGETS sort-visualizer-worker RUNTIME DESTINATION bin)
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
    ${COVERAGE_COMPILE_OPTIONS}
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
    ${PLATFORM_DL_LIB}
)
endif()

target_link_directories(sort-visualizer PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_directories(sort-visualizer PRIVATE ${GMODULE_LIBRARY_DIRS})
target_link_options(sort-visualizer PRIVATE ${COVERAGE_LINK_OPTIONS} ${GTK3_LDFLAGS_OTHER} ${GMODULE_LDFLAGS_OTHER})

if(NOT WIN32)
target_include_directories(sort-visualizer-worker PRIVATE include)

target_compile_options(sort-visualizer-worker PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    ${COVERAGE_COMPILE_OPTIONS}
)

target_link_options(sort-visualizer-worker PRIVATE ${COVERAGE_LINK_OPTIONS})
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
    ${COVERAGE_COMPILE_OPTIONS}
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
target_link_options(test-sort-unit PRIVATE ${COVERAGE_LINK_OPTIONS} ${GTK3_LDFLAGS_OTHER} ${GMODULE_LDFLAGS_OTHER})

add_executable(test-sort-integration
    tests/integration_custom_sort_test.c
    src/sort_engine.c
)

add_executable(test-ui-helpers
    tests/unit_main_helpers_test.c
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
    ${COVERAGE_COMPILE_OPTIONS}
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
target_link_options(test-sort-integration PRIVATE ${COVERAGE_LINK_OPTIONS} ${GTK3_LDFLAGS_OTHER} ${GMODULE_LDFLAGS_OTHER})

target_include_directories(test-ui-helpers PRIVATE
    include
    ${GTK3_INCLUDE_DIRS}
    ${GMODULE_INCLUDE_DIRS}
)

target_compile_options(test-ui-helpers PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    ${COVERAGE_COMPILE_OPTIONS}
    ${GTK3_CFLAGS_OTHER}
    ${GMODULE_CFLAGS_OTHER}
)

target_link_libraries(test-ui-helpers PRIVATE
    ${GTK3_LIBRARIES}
    ${GMODULE_LIBRARIES}
    ${PLATFORM_DL_LIB}
)

target_link_directories(test-ui-helpers PRIVATE ${GTK3_LIBRARY_DIRS})
target_link_directories(test-ui-helpers PRIVATE ${GMODULE_LIBRARY_DIRS})
target_link_options(test-ui-helpers PRIVATE ${COVERAGE_LINK_OPTIONS} ${GTK3_LDFLAGS_OTHER} ${GMODULE_LDFLAGS_OTHER})

if(NOT WIN32)
    add_dependencies(test-sort-integration sort-visualizer-worker)
endif()

add_test(NAME unit-sort-engine COMMAND test-sort-unit)
add_test(NAME integration-custom-sort COMMAND test-sort-integration)
add_test(NAME unit-ui-helpers COMMAND ${CMAKE_COMMAND} -E env NO_AT_BRIDGE=1 $<TARGET_FILE:test-ui-helpers>)

if(ENABLE_COVERAGE AND NOT WIN32)
    find_program(LCOV_EXECUTABLE lcov REQUIRED)
    find_program(GENHTML_EXECUTABLE genhtml REQUIRED)
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)

    set(COVERAGE_CTEST_COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure)
    if(XVFB_RUN_EXECUTABLE)
        set(COVERAGE_CTEST_COMMAND ${XVFB_RUN_EXECUTABLE} -a ${CMAKE_CTEST_COMMAND} --output-on-failure)
    endif()

    add_custom_target(coverage
        COMMAND ${LCOV_EXECUTABLE} --directory . --zerocounters
        COMMAND ${COVERAGE_CTEST_COMMAND}
        COMMAND ${LCOV_EXECUTABLE} --capture --directory . --output-file coverage.info
        COMMAND ${LCOV_EXECUTABLE} --extract coverage.info '*/src/main.c' '*/src/sort_engine.c' --output-file coverage.info
        COMMAND ${GENHTML_EXECUTABLE} coverage.info --output-directory coverage-html --title "sort-visualizer line coverage" --legend
        COMMAND ${LCOV_EXECUTABLE} --summary coverage.info --fail-under-lines 85
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS test-sort-unit test-sort-integration test-ui-helpers
        COMMENT "Running tests and enforcing >=85% line coverage for src/main.c and src/sort_engine.c (bogo excluded)"
    )
endif()
endif()
