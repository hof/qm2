
include_directories(${QM2_SOURCE_DIR}/src)
link_directories(${QM2_BINARY_DIR}/src)

add_executable(testBits test_bits.cpp)
add_executable(testGenmoves test_genmoves.cpp)
add_executable(testSearch test_search.cpp)
add_executable(testPolyglot test_polyglot.cpp)
add_executable(testTT test_transpositiontable.cpp)

target_link_libraries(testBits QM2SRC)
target_link_libraries(testGenmoves QM2SRC)
target_link_libraries(testSearch QM2SRC)
target_link_libraries(testPolyglot QM2SRC)
target_link_libraries(testTT QM2SRC)

#performance analyzing tests: 
add_executable(testDepth test_depth.cpp)
add_executable(testBK test_bratkokopec.cpp)
add_executable(testWAC test_wac.cpp)
add_executable(testPrintSearch test_printsearch.cpp)
add_executable(testEvaluation test_evaluation.cpp)

target_link_libraries(testDepth QM2SRC)
target_link_libraries(testWAC QM2SRC)
target_link_libraries(testBK QM2SRC)
target_link_libraries(testPrintSearch QM2SRC)
target_link_libraries(testEvaluation QM2SRC)

