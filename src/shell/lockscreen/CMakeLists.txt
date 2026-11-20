cmake_minimum_required(VERSION 3.16)
project(havel-lock CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Lock screen executable
add_executable(havel-lock
    main.cpp
    LockScreen.cpp
)

target_include_directories(havel-lock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(havel-lock PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Install
install(TARGETS havel-lock RUNTIME DESTINATION bin)
