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
