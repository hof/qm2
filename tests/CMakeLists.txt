
include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

add_executable(testBits test_bits.cpp)
add_executable(testGenmoves test_genmoves.cpp)
add_executable(testSearch test_search.cpp)
add_executable(testPolyglot test_polyglot.cpp)
add_executable(testTT test_transpositiontable.cpp)
add_executable(testFlip test_flip.cpp)

target_link_libraries(testBits MAX2SRC)
target_link_libraries(testGenmoves MAX2SRC)
target_link_libraries(testSearch MAX2SRC)
target_link_libraries(testPolyglot MAX2SRC)
target_link_libraries(testTT MAX2SRC)
target_link_libraries(testFlip MAX2SRC)


#performance analyzing tests: 
add_executable(testDepth test_depth.cpp)
add_executable(testBK test_bratkokopec.cpp)
add_executable(testWAC test_wac.cpp)
add_executable(testEvaluation test_evaluation.cpp)

target_link_libraries(testDepth MAX2SRC)
target_link_libraries(testWAC MAX2SRC)
target_link_libraries(testBK MAX2SRC)
target_link_libraries(testEvaluation MAX2SRC)

