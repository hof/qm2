
include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

add_executable(genPST genPST.cpp)
target_link_libraries(genPST MAX2SRC)