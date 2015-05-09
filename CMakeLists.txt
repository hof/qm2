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

configure_file ( 
	"${PROJECT_SOURCE_DIR}/src/version.h.in" 
	"${PROJECT_SOURCE_DIR}/src/version.h" 
)

# tests 
enable_testing()

add_test(testBits tests/testBits)
add_test(testSEE tests/testSEE)
add_test(testGenmoves tests/testGenmoves)
add_test(testSearch tests/testSearch)
add_test(testPolyglot tests/testPolyglot)
add_test(testTT tests/testTT)
add_test(testEvaluation tests/testEvaluation)
add_test(testFlip tests/testFlip)
add_test(testDepth tests/testDepth)
add_test(testPerformance tests/testPerformance)
add_test(testW17Performance tests/testW17Performance)
