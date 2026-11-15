find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(test_protocol test_protocol.cpp) # build the program from the file
target_link_libraries(test_protocol PRIVATE Qt6::Test onenode_core) # link it to test framework and core
add_test(NAME test_protocol COMMAND test_protocol) # launch this as a separate process

qt_add_executable(test_session test_session.cpp) # build the program from the file
target_link_libraries(test_session PRIVATE Qt6::Test onenode_core) # link it to test framework and core
add_test(NAME test_session COMMAND test_session) # launch this as a separate process

qt_add_executable(test_corebridge test_corebridge.cpp)
target_link_libraries(test_corebridge PRIVATE Qt6::Test onenode_core)
add_test(NAME test_corebridge COMMAND test_corebridge)

qt_add_executable(test_transfermodel test_transfermodel.cpp)
target_link_libraries(test_transfermodel PRIVATE Qt6::Test onenode_core)
add_test(NAME test_transfermodel COMMAND test_transfermodel)

qt_add_executable(test_identity test_identity.cpp)
target_link_libraries(test_identity PRIVATE Qt6::Test onenode_core)
add_test(NAME test_identity COMMAND test_identity)

qt_add_executable(test_tls test_tls.cpp)
target_link_libraries(test_tls PRIVATE Qt6::Test onenode_core)
add_test(NAME test_tls COMMAND test_tls)

qt_add_executable(test_truststore test_truststore.cpp)
target_link_libraries(test_truststore PRIVATE Qt6::Test onenode_core)
add_test(NAME test_truststore COMMAND test_truststore)

qt_add_executable(test_pairingengine test_pairingengine.cpp)
target_link_libraries(test_pairingengine PRIVATE Qt6::Test onenode_core)
add_test(NAME test_pairingengine COMMAND test_pairingengine)

qt_add_executable(test_pairingmode test_pairingmode.cpp)
target_link_libraries(test_pairingmode PRIVATE Qt6::Test onenode_core)
add_test(NAME test_pairingmode COMMAND test_pairingmode)

qt_add_executable(test_dnsmessage test_dnsmessage.cpp)
target_link_libraries(test_dnsmessage PRIVATE Qt6::Test onenode_core)
add_test(NAME test_dnsmessage COMMAND test_dnsmessage)

qt_add_executable(test_mdns test_mdns.cpp)
target_link_libraries(test_mdns PRIVATE Qt6::Test onenode_core)
add_test(NAME test_mdns COMMAND test_mdns)

qt_add_executable(test_mdns_udp test_mdns_udp.cpp)
target_link_libraries(test_mdns_udp PRIVATE Qt6::Test onenode_core)
add_test(NAME test_mdns_udp COMMAND test_mdns_udp)
