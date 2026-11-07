find_package(Qt6 REQUIRED COMPONENTS Test Network)

qt_add_executable(test_protocol
    test_protocol.cpp
    ${PROJECT_SOURCE_DIR}/src/core/source-files/protocol.cpp
)
target_include_directories(test_protocol PRIVATE ${PROJECT_SOURCE_DIR}/src/core/header-files)
target_link_libraries(test_protocol PRIVATE Qt6::Test)
add_test(NAME test_protocol COMMAND test_protocol)

qt_add_executable(test_session
    test_session.cpp
    ${PROJECT_SOURCE_DIR}/src/core/source-files/protocol.cpp
    ${PROJECT_SOURCE_DIR}/src/core/source-files/receivesession.cpp
    ${PROJECT_SOURCE_DIR}/src/core/source-files/sendsession.cpp
    ${PROJECT_SOURCE_DIR}/src/core/header-files/receivesession.h
    ${PROJECT_SOURCE_DIR}/src/core/header-files/sendsession.h
)
target_include_directories(test_session PRIVATE ${PROJECT_SOURCE_DIR}/src/core/header-files)
target_link_libraries(test_session PRIVATE Qt6::Test Qt6::Network)
add_test(NAME test_session COMMAND test_session)