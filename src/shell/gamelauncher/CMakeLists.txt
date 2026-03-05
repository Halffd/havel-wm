# Game Launcher CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(havel-gamelauncher VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Enable Qt MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Sources
set(SOURCES
    GameLauncher.cpp
)

# Create executable
add_executable(havel-gamelauncher ${SOURCES})

# Link libraries
target_link_libraries(havel-gamelauncher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# Install
install(TARGETS havel-gamelauncher
    RUNTIME DESTINATION bin
)

# Install desktop file
install(FILES havel-gamelauncher.desktop
    DESTINATION share/applications
)
