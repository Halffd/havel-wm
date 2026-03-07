cmake_minimum_required(VERSION 3.16)
project(havel-wm C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

# Find Wayland and wlroots
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(WLROOTS REQUIRED wlroots-0.20)
pkg_check_modules(XKB REQUIRED xkbcommon)
pkg_check_modules(PIXMAN REQUIRED pixman-1)
pkg_check_modules(FMT REQUIRED fmt)
pkg_check_modules(GLES2 REQUIRED glesv2)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(PNG REQUIRED libpng)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(VULKAN REQUIRED vulkan)

# Renderer selection (default: Vulkan with GLES2 fallback)
option(USE_VULKAN "Use Vulkan renderer (default ON)" ON)
option(USE_GLES2 "Use GLES2 renderer as fallback (default ON)" ON)

if(USE_VULKAN)
    add_compile_definitions(USE_VULKAN=1)
    message(STATUS "Vulkan renderer enabled")
else()
    add_compile_definitions(USE_VULKAN=0)
    message(STATUS "Vulkan renderer disabled")
endif()

if(USE_GLES2)
    add_compile_definitions(USE_GLES2=1)
    message(STATUS "GLES2 renderer enabled")
else()
    add_compile_definitions(USE_GLES2=0)
    message(STATUS "GLES2 renderer disabled")
endif()

# Find PipeWire for screen sharing
pkg_check_modules(PIPEWIRE libpipewire-0.3)
if(PIPEWIRE_FOUND)
    set(PIPEWIRE_ENABLED ON)
    message(STATUS "PipeWire found - screen sharing enabled")
else()
    set(PIPEWIRE_ENABLED OFF)
    message(STATUS "PipeWire not found - screen sharing disabled")
endif()

# Find GLib/GIO for D-Bus (xdg-desktop-portal integration)
pkg_check_modules(GLIB gio-2.0 gio-unix-2.0)
if(GLIB_FOUND)
    set(GLIB_ENABLED ON)
    message(STATUS "GLib/GIO found - xdg-desktop-portal integration enabled")
else()
    set(GLIB_ENABLED OFF)
    message(STATUS "GLib/GIO not found - xdg-desktop-portal integration disabled")
endif()

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

# Generate layer-shell protocol (unstable) - download from wlroots if not present
if(NOT EXISTS ${WAYLAND_PROTOCOLS_DIR}/unstable/wlr-layer-shell/unstable-v1/wlr-layer-shell-unstable-v1.xml)
    file(DOWNLOAD
        https://gitlab.freedesktop.org/wlroots/wlroots/-/raw/master/protocol/wlr-layer-shell-unstable-v1.xml
        ${CMAKE_BINARY_DIR}/wlr-layer-shell-unstable-v1.xml
        SHOW_PROGRESS
    )
    set(LAYER_SHELL_XML ${CMAKE_BINARY_DIR}/wlr-layer-shell-unstable-v1.xml)
else()
    set(LAYER_SHELL_XML ${WAYLAND_PROTOCOLS_DIR}/unstable/wlr-layer-shell/unstable-v1/wlr-layer-shell-unstable-v1.xml)
endif()

# Download text-input protocol if not present
if(NOT EXISTS ${WAYLAND_PROTOCOLS_DIR}/unstable/text-input/text-input-unstable-v3.xml)
    file(DOWNLOAD
        https://gitlab.freedesktop.org/wayland/wayland-protocols/-/raw/main/unstable/text-input/text-input-unstable-v3.xml
        ${CMAKE_BINARY_DIR}/text-input-unstable-v3.xml
        SHOW_PROGRESS
    )
    set(TEXT_INPUT_XML ${CMAKE_BINARY_DIR}/text-input-unstable-v3.xml)
else()
    set(TEXT_INPUT_XML ${WAYLAND_PROTOCOLS_DIR}/unstable/text-input/text-input-unstable-v3.xml)
endif()

execute_process(
    COMMAND ${WAYLAND_SCANNER} server-header
    ${LAYER_SHELL_XML}
    ${PROTOCOL_HEADERS_DIR}/wlr-layer-shell-unstable-v1-protocol.h
)

execute_process(
    COMMAND ${WAYLAND_SCANNER} private-code
    ${LAYER_SHELL_XML}
    ${PROTOCOL_HEADERS_DIR}/wlr-layer-shell-unstable-v1-protocol.c
)

# Generate text-input protocol headers
execute_process(
    COMMAND ${WAYLAND_SCANNER} server-header
    ${TEXT_INPUT_XML}
    ${PROTOCOL_HEADERS_DIR}/text-input-unstable-v3-protocol.h
)

execute_process(
    COMMAND ${WAYLAND_SCANNER} private-code
    ${TEXT_INPUT_XML}
    ${PROTOCOL_HEADERS_DIR}/text-input-unstable-v3-protocol.c
)

# Add the generated protocol source
set(PROTOCOL_SOURCES
    ${PROTOCOL_HEADERS_DIR}/xdg-shell-protocol.c
    ${PROTOCOL_HEADERS_DIR}/wlr-layer-shell-unstable-v1-protocol.c
    ${PROTOCOL_HEADERS_DIR}/text-input-unstable-v3-protocol.c
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
    src/wm/core/ScreenCapture.cpp
    src/wm/core/ScreenCapture.hpp
    src/wm/core/PipeWireStream.cpp
    src/wm/core/PipeWireStream.hpp
    src/wm/core/DesktopPortal.cpp
    src/wm/core/DesktopPortal.hpp
    src/wm/core/WindowGroupManager.cpp
    src/wm/core/WindowGroupManager.hpp
    # XWaylandDnD - WIP, needs wlroots integration
    # src/wm/core/XWaylandDnD.cpp
    # src/wm/core/XWaylandDnD.hpp
    src/wm/core/LoadingScreen.c
    src/wm/core/LoadingScreen.h
    src/wm/render/RenderPipeline.cpp
    src/wm/render/render_c_wrappers.c
    src/wm/render/ShaderEffect.cpp
    src/wm/render/AdvancedShaderEffects.cpp
    src/wm/render/OverlayRenderer.cpp
    src/wm/render/OverlayRenderer.hpp
    src/wm/render/OverlayRendering.cpp
    src/wm/render/BitmapFont.cpp
    src/wm/render/BitmapFont.hpp
    src/wm/render/FreeTypeFont.cpp
    src/wm/render/FreeTypeFont.hpp
    src/wm/render/AppIconLoader.cpp
    src/wm/render/BlurShader.cpp
    src/wm/render/BlurShader.hpp
    src/wm/input/InputManager.cpp
    src/wm/input/KeybindingManager.cpp
    src/wm/input/KeybindingManager.hpp
    src/wm/input/TextInputManager.cpp
    src/wm/input/GestureRecognizer.cpp
    src/wm/input/GestureRecognizer.hpp
    src/wm/input/InputDeviceManager.cpp
    src/wm/input/InputDeviceManager.hpp
    src/wm/input/ComboManager.cpp
    src/wm/input/ComboManager.hpp
    src/wm/overlay/OverlayScene.cpp
    src/wm/overlay/AltTabOverlay.cpp
    src/wm/overlay/OverviewOverlay.cpp
    src/wm/overlay/AppLauncherOverlay.cpp
    src/wm/desktop/DesktopManager.cpp
    src/wm/desktop/DesktopManager.hpp
    src/wm/desktop/NASAWallpaper.cpp
    src/wm/desktop/NASAWallpaper.hpp
    src/wm/desktop/NASAWallpaperTests.cpp
    src/wm/desktop/FileManager.cpp
    src/wm/desktop/FileManager.hpp
    src/wm/plugins/PluginManager.cpp
    src/wm/plugins/ExamplePlugin.cpp
    src/wm/plugins/BlurPlugin.cpp
    src/wm/plugins/ScalePlugin.cpp
    src/wm/plugins/WallpaperPlugin.cpp
    src/wm/plugins/NotificationsPlugin.cpp
    src/wm/plugins/CustomLayoutsPlugin.cpp
    src/wm/plugins/WindowSnapPlugin.cpp
    src/wm/plugins/HotCornersPlugin.cpp
    src/wm/plugins/GammaPlugin.cpp
    src/wm/plugins/AppLauncherPlugin.cpp
    src/wm/plugins/AltTabPlugin.cpp
    src/wm/plugins/OverviewPlugin.cpp
    src/wm/plugins/ServerDecorationPlugin.cpp
    src/wm/plugins/DrawPlugin.cpp
    src/wm/plugins/FPSPlugin.cpp
    src/wm/plugins/GammaPlugin.cpp
    src/wm/render/VulkanRenderer.c
    src/wm/render/VulkanRendererBridge.h
    src/wm/render/VulkanSceneCompositor.c
    src/wm/render/VulkanSceneCompositor.h
    src/wm/render/VulkanOverlayRenderer.c
    src/wm/render/VulkanOverlayRenderer.h
    src/wm/render/VulkanDmaBuf.c
    src/wm/render/VulkanDmaBuf.h
    src/wm/render/GLES2Renderer.c
    src/wm/render/GLES2Renderer.h
    src/wm/render/UnifiedRenderer.c
    src/wm/render/UnifiedRenderer.h
    src/wm/render/WlrBinding.c
    src/wm/render/WlrBinding.h
    src/wm/render/DamageTracker.c
    src/wm/render/DamageTracker.h
    src/wm/render/MultiGPU.c
    src/wm/render/MultiGPU.h
    src/wm/render/CompositorRenderer.c
    src/wm/render/CompositorRenderer.h
    # Animation plugins (WIP - need interface adjustments)
    # src/wm/plugins/WobblyWindowsPlugin.cpp
    # src/wm/plugins/DestroyOnClosePlugin.cpp
    # src/wm/plugins/SpringOnSpawnPlugin.cpp
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

# Wlroots C bridge library (isolates wlroots headers from C++)
set(WLR_BRIDGE_SOURCES
    wlr_bridge.c
)

add_library(wlr_bridge STATIC ${WLR_BRIDGE_SOURCES})
target_include_directories(wlr_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/include
    ${PROTOCOL_HEADERS_DIR}
)
target_include_directories(wlr_bridge PRIVATE
    ${WLROOTS_INCLUDE_DIRS}
    ${PROTOCOL_HEADERS_DIR}
    ${VULKAN_INCLUDE_DIRS}
    ${GLES2_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)
target_compile_definitions(wlr_bridge PRIVATE WLR_USE_UNSTABLE)
target_link_libraries(wlr_bridge
    ${WLROOTS_LIBRARIES}
    ${VULKAN_LIBRARIES}
    ${GLES2_LIBRARIES}
    ${EGL_LIBRARIES}
    dl
)
set_target_properties(wlr_bridge PROPERTIES
    LINKER_LANGUAGE C
)

# Add executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${WM_CORE_SOURCES}
    ${SHELL_SOURCES}
    ${UTIL_SOURCES}
    ${PROTOCOL_SOURCES}
)

# Set include directories for main executable
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${PROTOCOL_HEADERS_DIR}
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${XKB_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${GLES2_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

# Enable unstable wlroots features
target_compile_definitions(${PROJECT_NAME} PRIVATE WLR_USE_UNSTABLE)

# Link with wlroots bridge library and other libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    wlr_bridge
    ${WAYLAND_SERVER_LIBRARIES}
    ${XKB_LIBRARIES}
    ${PIXMAN_LIBRARIES}
    ${FMT_LIBRARIES}
    ${GLES2_LIBRARIES}
    ${EGL_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${PNG_LIBRARIES}
    ${CURL_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${GLIB_LIBRARIES}
)

# Set library directories
target_link_directories(${PROJECT_NAME} PRIVATE
    ${WAYLAND_SERVER_LIBRARY_DIRS}
    ${WLROOTS_LIBRARY_DIRS}
    ${XKB_LIBRARY_DIRS}
    ${PIXMAN_LIBRARY_DIRS}
    ${FMT_LIBRARY_DIRS}
    ${GLES2_LIBRARY_DIRS}
    ${EGL_LIBRARY_DIRS}
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
target_compile_options(${PROJECT_NAME}  PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
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

    # Build file manager
    add_subdirectory(src/shell/filemanager)

    # Build terminal
    add_subdirectory(src/shell/terminal)

    # Build screenshot
    add_subdirectory(src/shell/screenshot)

    # Build text editor
    add_subdirectory(src/shell/texteditor)

    # Build game launcher
    add_subdirectory(src/shell/gamelauncher)

    # Build dictionary
    add_subdirectory(src/shell/dictionary)

    # Build updater
    add_subdirectory(src/shell/updater)

    # Build video player
    add_subdirectory(src/shell/videoplayer)

    # Build and install settings
    add_subdirectory(src/shell/settings)

    # Build and install lock screen
    add_subdirectory(src/shell/lockscreen)

    # Build utilities test
    add_executable(test-utilities tests/test_utilities.c)
    target_include_directories(test-utilities PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    )
    target_link_libraries(test-utilities m)
endif()
