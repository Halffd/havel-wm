# Video Player CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(havel-videoplayer VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6 Multimedia
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia)

# Enable Qt MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Sources
set(SOURCES
    main.cpp
    VideoPlayer.cpp
)

set(HEADERS
    VideoPlayer.hpp
)

# Create executable
add_executable(havel-videoplayer ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(havel-videoplayer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
)

# Install
install(TARGETS havel-videoplayer
    RUNTIME DESTINATION bin
)

# Install desktop file
install(FILES havel-videoplayer.desktop
    DESTINATION share/applications
)
