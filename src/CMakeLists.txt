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
        eval_material.cpp
        eval_pieces.cpp
        experience.cpp
        tablebase.cpp
        eval_endgame.cpp
        eval_pawns.cpp
        eval_king_attack.cpp
	eval.cpp
	search.cpp
        game.cpp      
        engine.cpp
        uci_console.cpp
        w17/w17_search.cpp
        w17/w17_eval.cpp
)

include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

add_executable(maxima2 main.cpp)

add_library(fathom STATIC ../third_party/fathom/tbprobe.c)
target_include_directories(fathom PUBLIC ../third_party/fathom)
target_link_libraries(fathom PRIVATE pthread)
target_link_libraries(MAX2SRC pthread fathom)
target_link_libraries(maxima2 MAX2SRC)

install (TARGETS maxima2 RUNTIME DESTINATION bin)
