add_library(MAX2SRC 
        bbmoves.cpp	
	hashcodes.cpp
        move.cpp
	board.cpp
	timeman.cpp	
        hashtable.cpp
	movegen.cpp
	movepicker.cpp
	book.cpp
        experience.cpp
        tablebase.cpp
        eval_endgame.cpp
        nnue.cpp
	eval.cpp
	search.cpp
        repetition.cpp
        game.cpp      
        engine.cpp
        uci_console.cpp
        w17/w17_search.cpp
        w17/w17_eval.cpp
)

target_include_directories(MAX2SRC PUBLIC "${PROJECT_BINARY_DIR}/generated")

include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

add_executable(maxima2 main.cpp)

add_library(fathom STATIC ../third_party/fathom/tbprobe.c)
target_include_directories(fathom PUBLIC ../third_party/fathom)
target_link_libraries(fathom PRIVATE pthread)
target_link_libraries(MAX2SRC pthread fathom)
target_link_libraries(maxima2 MAX2SRC)

if(NOT maxima_pgo_mode STREQUAL "off")
    foreach(maxima_target IN ITEMS MAX2SRC maxima2 fathom)
        target_compile_options(${maxima_target} PRIVATE
            "-fprofile-${maxima_pgo_mode}=${maxima_pgo_dir}"
            "-fprofile-prefix-path=${CMAKE_BINARY_DIR}")
        if(maxima_pgo_mode STREQUAL "generate")
            target_compile_options(${maxima_target} PRIVATE -fprofile-update=atomic)
        else()
            target_compile_options(${maxima_target} PRIVATE
                -Werror=missing-profile -Werror=coverage-mismatch)
        endif()
    endforeach()
    # Consumers of the instrumented static library also need the GCC runtime.
    target_link_options(MAX2SRC INTERFACE
        "-fprofile-${maxima_pgo_mode}=${maxima_pgo_dir}")
endif()

install (TARGETS maxima2 RUNTIME DESTINATION bin)
