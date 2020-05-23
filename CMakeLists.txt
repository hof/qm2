cmake_minimum_required (VERSION 2.8.4)
project (MAX2)

add_compile_options(-march=native)

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
add_subdirectory(gen)
add_subdirectory(tests)

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
    "${PROJECT_SOURCE_DIR}/src/version.h"
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# tests
enable_testing()

add_test(testuBits tests/testuBits)
add_test(testuSEE tests/testuSEE)
add_test(testuGenmoves tests/testuGenmoves)
add_test(testuSearch tests/testuSearch)
add_test(testuPolyglot tests/testuPolyglot)
add_test(testuTT tests/testuTT)
add_test(testuEvaluation tests/testuEvaluation)
add_test(testuFlip tests/testuFlip)
add_test(testiDepth tests/testiDepth)
add_test(testiPerformance tests/testiPerformance)
add_test(testiW17Performance tests/testiW17Performance)
