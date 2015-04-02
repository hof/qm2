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
        eval_endgame.cpp
        eval_pawns.cpp
	eval.cpp
	search.cpp
        opponent.cpp
        game.cpp      
        engine.cpp
        uci_console.cpp
        w17/w17_search.cpp
        w17/w17_eval.cpp
)

include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

add_executable(maxima2 main.cpp)

target_link_libraries(MAX2SRC pthread)
target_link_libraries(maxima2 MAX2SRC)

install (TARGETS maxima2 RUNTIME DESTINATION bin)
