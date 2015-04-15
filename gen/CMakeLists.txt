include_directories(${MAX2_SOURCE_DIR}/src)
link_directories(${MAX2_BINARY_DIR}/src)

add_executable(genKPK genKPK.cpp)

target_link_libraries(genKPK MAX2SRC)