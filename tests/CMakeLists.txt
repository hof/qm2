
include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

# unit tests 
add_executable(testuBits test_bits.cpp)
add_executable(testuSEE test_see.cpp)
add_executable(testuGenmoves test_genmoves.cpp)
add_executable(testuSearch test_search.cpp)
add_executable(testuPolyglot test_polyglot.cpp)
add_executable(testuTT test_transpositiontable.cpp)
add_executable(testuFlip test_flip.cpp)
# other tests: 
add_executable(testiEvaluation test_evaluation.cpp)
add_executable(testiDepth test_depth.cpp)
add_executable(testiPerformance test_performance.cpp)
add_executable(testiW17Performance test_w17_performance.cpp)

target_link_libraries(testuBits MAX2SRC)
target_link_libraries(testuSEE MAX2SRC)
target_link_libraries(testuGenmoves MAX2SRC)
target_link_libraries(testuSearch MAX2SRC)
target_link_libraries(testuPolyglot MAX2SRC)
target_link_libraries(testuTT MAX2SRC)
target_link_libraries(testuFlip MAX2SRC)
target_link_libraries(testiEvaluation MAX2SRC)
target_link_libraries(testiDepth MAX2SRC)
target_link_libraries(testiPerformance MAX2SRC)
target_link_libraries(testiW17Performance MAX2SRC)
