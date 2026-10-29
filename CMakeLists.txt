cmake_minimum_required (VERSION 3.30)
project (MAX2 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 26 CACHE STRING "C++ language standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-march=native)

# Like -march=native, size cache-bound tables for the build machine's per-core L2.
set(maxima_l2_cache_size "" CACHE STRING "Per-core L2 cache bytes; empty detects the build machine")
set(maxima_l2 "${maxima_l2_cache_size}")
if(NOT maxima_l2)
    execute_process(COMMAND getconf LEVEL2_CACHE_SIZE
        OUTPUT_VARIABLE maxima_l2 OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT maxima_l2 MATCHES "^[0-9]+$")
    set(maxima_l2 0)
endif()
add_compile_definitions(MAXIMA_L2_CACHE_SIZE=${maxima_l2})

option(maxima_lto "Enable link-time optimization for Release builds" OFF)
set(maxima_pgo_mode "off" CACHE STRING "GCC Release profiling: off, generate, or use")
set_property(CACHE maxima_pgo_mode PROPERTY STRINGS off generate use)
set(maxima_pgo_dir "" CACHE PATH "Directory containing GCC training profiles")

if(NOT maxima_pgo_mode MATCHES "^(off|generate|use)$")
    message(FATAL_ERROR "maxima_pgo_mode must be off, generate, or use")
endif()
if(NOT maxima_pgo_mode STREQUAL "off")
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release"
            OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
            OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "PGO requires a Release build with GCC for C and C++")
    endif()
    if(NOT maxima_pgo_dir)
        message(FATAL_ERROR "PGO requires maxima_pgo_dir")
    endif()
    get_filename_component(maxima_pgo_dir "${maxima_pgo_dir}" ABSOLUTE
        BASE_DIR "${CMAKE_BINARY_DIR}")
    if(maxima_pgo_mode STREQUAL "use")
        file(GLOB_RECURSE maxima_pgo_profiles "${maxima_pgo_dir}/*.gcda")
        if(NOT maxima_pgo_profiles)
            message(FATAL_ERROR "No GCC training profiles found in maxima_pgo_dir")
        endif()
    endif()
endif()

if(maxima_lto)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT maxima_ipo_supported OUTPUT maxima_ipo_error
        LANGUAGES C CXX)
    if(NOT maxima_ipo_supported)
        message(FATAL_ERROR "LTO is unavailable: ${maxima_ipo_error}")
    endif()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
message(STATUS "Compiler optimization: LTO=${maxima_lto}, PGO=${maxima_pgo_mode}")

add_custom_target(debug
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Debug ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
    COMMENT "Switch CMAKE_BUILD_TYPE to Debug"
)

add_custom_target(release
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
    COMMENT "Switch CMAKE_BUILD_TYPE to Release"
)

add_definitions("-Wall -Wextra")
add_subdirectory(src)
include(CTest)
if(BUILD_TESTING)
    set(maxima_nnue_test_network "${PROJECT_SOURCE_DIR}/networks/nn-62ef826d1a6d.nnue"
        CACHE FILEPATH "HalfKP network for standard search and reference checks")
    if(NOT maxima_nnue_test_network)
        set(maxima_nnue_test_network "${PROJECT_SOURCE_DIR}/networks/nn-62ef826d1a6d.nnue"
            CACHE FILEPATH "HalfKP network for standard search and reference checks" FORCE)
    endif()
    if(NOT EXISTS "${maxima_nnue_test_network}")
        message(FATAL_ERROR "Search tests require maxima_nnue_test_network; run tools/download_nnue.py or set BUILD_TESTING=OFF")
    endif()
    add_subdirectory(tests)
endif()

find_package(Git)
if (GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Git revision: ${REVISION}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE)
        message(STATUS "Git branch: ${GIT_BRANCH}")
else()
    set(REVISION "unknown")
    set(GIT_BRANCH "unknown")
endif()

SET(CMAKE_CXX_FLAGS_COVERAGE
    "-g -O0 --coverage -fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the C++ compiler during coverage builds."
    FORCE )

configure_file (
    "${PROJECT_SOURCE_DIR}/src/version.h.in"
    "${PROJECT_BINARY_DIR}/generated/maxima/version.h"
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Engine correctness checks use only C++ and Python standard-library fixtures.
if(BUILD_TESTING)
    find_package(Python3 3.7 REQUIRED COMPONENTS Interpreter)

    add_test(testBits tests/testBits)
    add_test(testSEE tests/testSEE)
    add_test(testGenmoves tests/testGenmoves)
    add_test(test_board_state tests/board_oracle_probe --self-test)
    set_tests_properties(test_board_state PROPERTIES TIMEOUT 10)
    add_test(testSearch tests/testSearch)
    add_test(testPolyglot tests/testPolyglot)
    add_test(testTT tests/testTT)
    add_test(testEvaluation tests/testEvaluation)
    add_test(test_nnue tests/test_nnue)
    add_test(testFlip tests/testFlip)
    add_test(testMates tests/testMates)
    add_test(test_leaf_search tests/test_leaf_search)
    add_test(test_search_ownership tests/test_search_ownership)
    add_test(test_search_pv tests/test_search_pv)
    add_test(test_search_repetition tests/test_search_repetition)
    add_test(test_search_tt_context tests/test_search_tt_context)
    add_test(test_search_ceiling tests/test_search_ceiling)
    add_test(test_search_boundary tests/test_search_boundary)
    add_test(test_search_tree tests/test_search_tree)
    add_test(test_search_horizon tests/test_search_horizon)
    add_test(test_search_review tests/test_search_review)
    add_test(test_w17_tt_context tests/test_w17_tt_context)
    add_test(test_w17_evaluation tests/test_w17_evaluation)
    add_test(test_tablebase tests/test_tablebase)
    set_tests_properties(test_search_ownership PROPERTIES TIMEOUT 10)
    add_test(test_search_lifecycle ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/test_search_lifecycle.py
        ${PROJECT_BINARY_DIR}/src/maxima2)
    set_tests_properties(test_search_lifecycle PROPERTIES TIMEOUT 90)
    if(maxima_nnue_test_network)
        add_test(NAME test_nnue_lifecycle COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/test_nnue_lifecycle.py
            ${PROJECT_BINARY_DIR}/src/maxima2 "${maxima_nnue_test_network}")
        set_tests_properties(test_nnue_lifecycle PROPERTIES TIMEOUT 180)
    endif()

    add_test(test_uci_input tests/test_uci_input)
    add_test(test_uci_output tests/test_uci_output)
    set_tests_properties(test_uci_output PROPERTIES TIMEOUT 10)
    get_property(maxima_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${maxima_tests} PROPERTIES
        ENVIRONMENT "MAXIMA_EVAL_FILE=${maxima_nnue_test_network}")
endif()
