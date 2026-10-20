
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

add_executable(test_w17_evaluation test_w17_evaluation.cpp)
target_link_libraries(test_w17_evaluation MAX2SRC)

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

add_executable(test_search_tree test_search_tree.cpp)
target_link_libraries(test_search_tree MAX2SRC)

add_executable(test_search_selectivity test_search_selectivity.cpp)
target_link_libraries(test_search_selectivity MAX2SRC)

add_executable(test_search_horizon test_search_horizon.cpp)
target_link_libraries(test_search_horizon MAX2SRC)

add_executable(test_search_review test_search_review.cpp)
target_link_libraries(test_search_review MAX2SRC)

add_executable(test_nnue test_nnue.cpp)
target_link_libraries(test_nnue MAX2SRC)
if(maxima_nnue_search_audit)
    if(NOT maxima_nnue_test_network)
        message(FATAL_ERROR "NNUE search audit tests require maxima_nnue_test_network")
    endif()
    add_executable(nnue_search_probe nnue_search_probe.cpp)
    target_link_libraries(nnue_search_probe MAX2SRC)
    add_executable(test_nnue_hash_ordering test_nnue_hash_ordering.cpp)
    target_link_libraries(test_nnue_hash_ordering MAX2SRC)
    add_test(NAME test_nnue_hash_ordering COMMAND test_nnue_hash_ordering "${maxima_nnue_test_network}")
    add_executable(test_nnue_search_audit test_nnue_search_audit.cpp)
    target_link_libraries(test_nnue_search_audit MAX2SRC)
    add_test(NAME test_nnue_search_audit COMMAND test_nnue_search_audit "${maxima_nnue_test_network}")
endif()

if(maxima_nnue_test_network)
    add_library(nnue_reference STATIC
        ../third_party/nnue_reference/nnue.cpp
        ../third_party/nnue_reference/misc.cpp)
    # Scalar reference deliberately differs from Maxima's native SIMD path.
    target_compile_definitions(nnue_reference PRIVATE IS_64BIT)
    add_executable(test_nnue_reference test_nnue_reference.cpp)
    target_link_libraries(test_nnue_reference MAX2SRC nnue_reference)
    add_test(NAME test_nnue_reference COMMAND test_nnue_reference "${maxima_nnue_test_network}")
endif()

add_executable(test_search_aspiration test_search_aspiration.cpp)
target_link_libraries(test_search_aspiration MAX2SRC)
add_test(NAME test_search_aspiration COMMAND test_search_aspiration)

add_executable(test_search_iid test_search_iid.cpp)
target_link_libraries(test_search_iid MAX2SRC)
add_test(NAME test_search_iid COMMAND test_search_iid)

add_executable(test_move_picker test_move_picker.cpp)
target_link_libraries(test_move_picker MAX2SRC)
add_test(NAME test_move_picker COMMAND test_move_picker)

add_executable(test_qsearch_tt test_qsearch_tt.cpp)
target_link_libraries(test_qsearch_tt MAX2SRC)
add_test(NAME test_qsearch_tt COMMAND test_qsearch_tt)

add_executable(test_search_node_context test_search_node_context.cpp)
target_link_libraries(test_search_node_context MAX2SRC)
add_test(NAME test_search_node_context COMMAND test_search_node_context)

add_executable(test_search_verification test_search_verification.cpp)
target_link_libraries(test_search_verification MAX2SRC)
add_test(NAME test_search_verification COMMAND test_search_verification)


add_executable(test_upcoming_repetition test_upcoming_repetition.cpp)
target_link_libraries(test_upcoming_repetition MAX2SRC)
add_test(NAME test_upcoming_repetition COMMAND test_upcoming_repetition)

get_property(maxima_subdirectory_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${maxima_subdirectory_tests} PROPERTIES
    ENVIRONMENT "MAXIMA_EVAL_FILE=${maxima_nnue_test_network}")
