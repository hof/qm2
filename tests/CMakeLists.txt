
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
add_executable(test_leaf_search test_leaf_search.cpp)
add_executable(test_search_ownership test_search_ownership.cpp)
add_executable(test_search_history test_search_history.cpp)
add_executable(test_search_pv test_search_pv.cpp)
add_executable(test_search_repetition test_search_repetition.cpp)

target_link_libraries(testBits MAX2SRC)
target_link_libraries(testSEE MAX2SRC)
target_link_libraries(testGenmoves MAX2SRC)
target_link_libraries(testSearch MAX2SRC)
target_link_libraries(testPolyglot MAX2SRC)
target_link_libraries(testTT MAX2SRC)
target_link_libraries(testFlip MAX2SRC)
target_link_libraries(testEvaluation MAX2SRC)
target_link_libraries(testMates MAX2SRC)
target_link_libraries(test_leaf_search MAX2SRC)
target_link_libraries(test_search_ownership MAX2SRC)
target_link_libraries(test_search_history MAX2SRC)
target_link_libraries(test_search_pv MAX2SRC)
target_link_libraries(test_search_repetition MAX2SRC)

add_executable(test_w17_tt_context test_w17_tt_context.cpp)
target_link_libraries(test_w17_tt_context MAX2SRC)

add_executable(test_search_tt_context test_search_tt_context.cpp)
target_link_libraries(test_search_tt_context MAX2SRC)

add_executable(test_null_search test_null_search.cpp)
target_link_libraries(test_null_search MAX2SRC)

add_executable(test_search_ceiling test_search_ceiling.cpp)
target_link_libraries(test_search_ceiling MAX2SRC)

add_executable(test_search_boundary test_search_boundary.cpp)
target_link_libraries(test_search_boundary MAX2SRC)

add_executable(test_kpk test_kpk.cpp)
target_link_libraries(test_kpk MAX2SRC)

add_executable(test_tablebase test_tablebase.cpp)
target_link_libraries(test_tablebase MAX2SRC)
target_compile_definitions(test_tablebase PRIVATE TABLEBASE_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/syzygy")

add_executable(test_experience test_experience.cpp)
target_link_libraries(test_experience MAX2SRC)

add_executable(test_uci_input test_uci_input.cpp)
target_link_libraries(test_uci_input MAX2SRC)

add_executable(test_uci_output test_uci_output.cpp)
target_link_libraries(test_uci_output MAX2SRC)

# Invoked by the independent runner checks; does not require python-chess here.
add_executable(board_oracle_probe board_oracle_probe.cpp)
target_link_libraries(board_oracle_probe MAX2SRC)
