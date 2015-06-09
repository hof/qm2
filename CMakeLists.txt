cmake_minimum_required (VERSION 2.8.4) 
project (MAX2)
add_definitions("-Wall -Wextra")
add_subdirectory(src)
add_subdirectory(gen)
add_subdirectory(tests)

find_package(Git)
if (GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Git revision is ${REVISION}")
else()
    set(REVISION "none")
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
