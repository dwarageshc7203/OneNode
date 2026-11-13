cmake_minimum_required(VERSION 3.16)

project(OneNode VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network)

qt_standard_project_setup(REQUIRES 6.8)

# The core: everything that is not UI. The app, the CLI and the tests all link this one library.
# STATIC compiles all these cpp files into one archive

add_library(onenode_core STATIC
    src/core/source-files/corebridge.cpp
    src/core/source-files/protocol.cpp
    src/core/source-files/receivesession.cpp
    src/core/source-files/sendsession.cpp
    src/core/header-files/corebridge.h
    src/core/header-files/netutil.h
    src/core/header-files/progressthrottle.h
    src/core/header-files/protocol.h
    src/core/header-files/receivesession.h
    src/core/header-files/sendsession.h
    src/core/header-files/transfermodel.h
    src/core/source-files/transfermodel.cpp
)

# PUBLIC - anyone can access
target_include_directories(onenode_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core/header-files)
target_link_libraries(onenode_core PUBLIC Qt6::Network)

# PRIVATE - only I can use it
# contains the CLI's source file with the core
qt_add_executable(onenode-cli src/cli/main.cpp)
target_link_libraries(onenode-cli PRIVATE onenode_core)

# GUI program bundled as OneNode so main.cpp load it by name instead of file path
qt_add_executable(appOneNode src/main.cpp)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(appOneNode
    URI OneNode
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/AppButton.qml
        qml/AppSwitch.qml
        qml/Card.qml
        qml/Toast.qml
        qml/TransferRow.qml
        qml/HomePage.qml
        qml/SendPage.qml
        qml/SettingsPage.qml
)

# fonts
qt_add_resources(appOneNode "fonts"
    PREFIX "/fonts"
    BASE qml/fonts
    FILES
        qml/fonts/Inter-Regular.otf
        qml/fonts/Inter-Medium.otf
        qml/fonts/Inter-SemiBold.otf
        qml/fonts/Inter-Bold.otf
)

# for MacOS and Windows, not linux
set_target_properties(appOneNode PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION} # writes version number into the app's label over the bundle on Mac
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE # for Mac, makes an .app instead of loose executable
    WIN32_EXECUTABLE TRUE # for Windows, builds GUI
)

# linking Qt Quick and core
target_link_libraries(appOneNode PRIVATE Qt6::Quick onenode_core)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(appOneNode PRIVATE -Wall -Wextra)
endif()

include(GNUInstallDirs)

#rules for cmake --install
install(TARGETS appOneNode
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

#switches on CTest
enable_testing()

#runs tests/CMakeLists.txt
add_subdirectory(tests)

