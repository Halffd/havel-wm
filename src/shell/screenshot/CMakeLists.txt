# Screenshot CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(havel-screenshot VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Enable Qt MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Sources
set(SOURCES
    main.cpp
    Screenshot.cpp
)

set(HEADERS
    Screenshot.hpp
)

# Create executable
add_executable(havel-screenshot ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(havel-screenshot PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Install
install(TARGETS havel-screenshot
    RUNTIME DESTINATION bin
)

# Install desktop file
install(FILES havel-screenshot.desktop
    DESTINATION share/applications
)
