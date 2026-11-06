find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(test_protocol
    test_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/core/protocol.cpp
)

target_include_directories(test_protocol PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(test_protocol PRIVATE Qt6::Test)

add_test(NAME test_protocol COMMAND test_protocol)
