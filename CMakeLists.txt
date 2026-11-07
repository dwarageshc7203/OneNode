cmake_minimum_required(VERSION 3.16)

project(OneNode VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appOneNode
    src/main.cpp
)

qt_add_qml_module(appOneNode
    URI OneNode
    QML_FILES
        qml/Main.qml
        SOURCES
            src/core/header-files/corebridge.h
            src/core/source-files/corebridge.cpp
            src/core/header-files/receivesession.h
            src/core/source-files/receivesession.cpp
            src/core/header-files/sendsession.h
            src/core/source-files/sendsession.cpp
            src/core/header-files/protocol.h
            src/core/source-files/protocol.cpp
            src/core/header-files/netutil.h

)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appOneNode PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appOneNode
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appOneNode
    PRIVATE Qt6::Quick Qt6::Network
)

target_include_directories(appOneNode PRIVATE ${PROJECT_SOURCE_DIR}/src/core/header-files)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(appOneNode PRIVATE -Wall -Wextra)
endif()

include(GNUInstallDirs)
install(TARGETS appOneNode
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

enable_testing()
add_subdirectory(tests)

