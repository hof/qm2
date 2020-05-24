
include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

# unit tests 
add_executable(testBits test_bits.cpp)
add_executable(testSEE test_see.cpp)
add_executable(testGenmoves test_genmoves.cpp)
add_executable(testSearch test_search.cpp)
add_executable(testPolyglot test_polyglot.cpp)
add_executable(testTT test_transpositiontable.cpp)
add_executable(testFlip test_flip.cpp)
add_executable(testEvaluation test_evaluation.cpp)
add_executable(testMates test_mates.cpp)

target_link_libraries(testBits MAX2SRC)
target_link_libraries(testSEE MAX2SRC)
target_link_libraries(testGenmoves MAX2SRC)
target_link_libraries(testSearch MAX2SRC)
target_link_libraries(testPolyglot MAX2SRC)
target_link_libraries(testTT MAX2SRC)
target_link_libraries(testFlip MAX2SRC)
target_link_libraries(testEvaluation MAX2SRC)
target_link_libraries(testMates MAX2SRC)