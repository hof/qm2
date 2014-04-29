add_library(QM2SRC 
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

include_directories(${QM2_SOURCE_DIR}/src)
link_directories(${QM2_BINARY_DIR}/src)

add_executable(qm2 main.cpp)

target_link_libraries(QM2SRC pthread)
target_link_libraries(qm2 QM2SRC)
