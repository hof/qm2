include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

add_executable(genPST genPST.cpp)
add_executable(genKPK genKPK.cpp)

target_link_libraries(genPST MAX2SRC)
target_link_libraries(genKPK MAX2SRC)