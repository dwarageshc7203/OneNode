find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(test_protocol test_protocol.cpp) # build the program from the file
target_link_libraries(test_protocol PRIVATE Qt6::Test onenode_core) # link it to test framework and core
add_test(NAME test_protocol COMMAND test_protocol) # launch this as a separate process

qt_add_executable(test_session test_session.cpp) # build the program from the file
target_link_libraries(test_session PRIVATE Qt6::Test onenode_core) # link it to test framework and core
add_test(NAME test_session COMMAND test_session) # launch this as a separate process
