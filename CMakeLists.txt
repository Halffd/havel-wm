cmake_minimum_required(VERSION 3.16)
project(havel-wm C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)

# Find Wayland and wlroots
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(WLROOTS REQUIRED wlroots-0.20)
pkg_check_modules(XKB REQUIRED xkbcommon)
pkg_check_modules(PIXMAN REQUIRED pixman-1)
pkg_check_modules(FMT REQUIRED fmt)
pkg_check_modules(GLES2 REQUIRED glesv2)

# Find Qt6 for panel
find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
if(Qt6_FOUND)
    set(PANEL_ENABLED ON)
else()
    set(PANEL_ENABLED OFF)
    message(STATUS "Qt6 not found - panel will not be built")
endif()

# Get wayland-protocols datadir for protocol files
execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pkgdatadir wayland-protocols
    OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Generate protocol headers
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTOCOL_HEADERS_DIR ${CMAKE_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${PROTOCOL_HEADERS_DIR})

# Generate xdg-shell protocol
execute_process(
    COMMAND ${WAYLAND_SCANNER} server-header
    ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml
    ${PROTOCOL_HEADERS_DIR}/xdg-shell-protocol.h
)

execute_process(
    COMMAND ${WAYLAND_SCANNER} private-code
    ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml
    ${PROTOCOL_HEADERS_DIR}/xdg-shell-protocol.c
)

# Add the generated protocol source
set(PROTOCOL_SOURCES
    ${PROTOCOL_HEADERS_DIR}/xdg-shell-protocol.c
)

# C++ WM core sources
set(WM_CORE_SOURCES
    src/wm/core/View.cpp
    src/wm/core/Workspace.cpp
    src/wm/core/Layout.cpp
    src/wm/core/Focus.cpp
    src/wm/core/Server.cpp
    src/wm/core/bridge.cpp
    src/wm/core/Animation.cpp
    src/wm/core/Animator.cpp
    src/wm/render/RenderPipeline.cpp
    src/wm/render/render_c_wrappers.c
    src/wm/render/ShaderEffect.cpp
    src/wm/input/InputManager.cpp
    src/wm/overlay/OverlayScene.cpp
    # VulkanRenderer.cpp - disabled, wlroots handles rendering
)

# Shell/panel sources (Phase 1 - window manager integration)
set(SHELL_SOURCES
    src/shell/WindowManager.cpp
    src/shell/IPCServer.cpp
)

# Utility sources
set(UTIL_SOURCES
    src/utils/Logger.c
)

# Add executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    wlr_bridge.c
    ${WM_CORE_SOURCES}
    ${SHELL_SOURCES}
    ${UTIL_SOURCES}
    ${PROTOCOL_SOURCES}
)

# Set compile definitions for unstable API
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WLR_USE_UNSTABLE
)

# Set include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${PROTOCOL_HEADERS_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${WLROOTS_INCLUDE_DIRS}
    ${XKB_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${GLES2_INCLUDE_DIRS}
    # UI framework include (for future use)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/include
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    ${WAYLAND_SERVER_LIBRARIES}
    ${WLROOTS_LIBRARIES}
    ${XKB_LIBRARIES}
    ${PIXMAN_LIBRARIES}
    ${FMT_LIBRARIES}
    ${GLES2_LIBRARIES}
)

# Set library directories
target_link_directories(${PROJECT_NAME} PRIVATE
    ${WAYLAND_SERVER_LIBRARY_DIRS}
    ${WLROOTS_LIBRARY_DIRS}
    ${XKB_LIBRARY_DIRS}
    ${PIXMAN_LIBRARY_DIRS}
    ${FMT_LIBRARY_DIRS}
    ${GLES2_LIBRARY_DIRS}
)

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE
    ${WAYLAND_SERVER_CFLAGS_OTHER}
    ${WLROOTS_CFLAGS_OTHER}
    ${XKB_CFLAGS_OTHER}
    ${PIXMAN_CFLAGS_OTHER}
    ${FMT_CFLAGS_OTHER}
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-c99-extensions
)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME "havel-wm"
)

# Install targets
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Install session file
install(FILES havel-wm.desktop
    DESTINATION share/wayland-sessions
)

# Build panel if Qt6 is available
if(PANEL_ENABLED)
    add_subdirectory(src/shell/panel)
    
    # Install panel target
    install(TARGETS havel-panel
        RUNTIME DESTINATION bin
    )
    
    # Build and install settings
    add_subdirectory(src/shell/settings)
    
    # Build and install lock screen
    add_subdirectory(src/shell/lockscreen)
endif()
