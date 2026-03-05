# Text Editor CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(havel-texteditor VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)

# Enable Qt MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Sources
set(SOURCES
    TextEditor.cpp
)

# Create executable
add_executable(havel-texteditor ${SOURCES})

# Link libraries
target_link_libraries(havel-texteditor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
)

# Install
install(TARGETS havel-texteditor
    RUNTIME DESTINATION bin
)

# Install desktop file
install(FILES havel-texteditor.desktop
    DESTINATION share/applications
)
