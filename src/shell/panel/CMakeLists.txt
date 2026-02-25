cmake_minimum_required(VERSION 3.16)
project(havel-panel CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Find IPC socket headers
set(HAVEL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Panel executable
add_executable(havel-panel
    main.cpp
    PanelWindow.cpp
    WindowButton.cpp
    IPCClient.cpp
)

target_include_directories(havel-panel PRIVATE
    ${HAVEL_INCLUDE_DIR}/src/shell/include
)

target_link_libraries(havel-panel PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# Install
install(TARGETS havel-panel RUNTIME DESTINATION bin)
