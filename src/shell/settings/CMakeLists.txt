cmake_minimum_required(VERSION 3.16)
project(havel-settings CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Settings executable
add_executable(havel-settings
    main.cpp
    SettingsWindow.cpp
)

target_include_directories(havel-settings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(havel-settings PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Install
install(TARGETS havel-settings RUNTIME DESTINATION bin)
