cmake_minimum_required(VERSION 3.16)
project(dropin)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Gui)
add_executable(dropin
    main.cpp
    mainwindow.cpp
    pairingserver.cpp
    filetransfer.cpp
    resources.qrc
)
target_link_libraries(dropin Qt6::Core Qt6::Widgets Qt6::Network Qt6::Gui)