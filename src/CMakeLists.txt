add_library(MAX2SRC 
        uci_console.cpp
	inputhandler.cpp
	outputhandler.cpp	
	hashcodes.cpp
	hashtable.cpp
	bbmoves.cpp
	move.cpp
	movegen.cpp
	movepicker.cpp
	board.cpp
	engine.cpp
	book.cpp
	evaluate.cpp
	search.cpp
	timemanager.cpp
)

include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

add_executable(maxima2 main.cpp)

target_link_libraries(MAX2SRC pthread)
target_link_libraries(maxima2 MAX2SRC)
