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
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(PAM REQUIRED pam)
pkg_check_modules(PNG REQUIRED libpng)
pkg_check_modules(CURL REQUIRED libcurl)

# Find nlohmann/json for JSON parsing
find_package(nlohmann_json CONFIG REQUIRED)

# OpenCV is only needed when the monorepo archives were built WITH it
# (HAVE_OPENCV); archives rebuilt without it carry no cv:: references and
# link fine with no opencv at all. Link only what pkg-config can find
# (OpenCV 5 ships opencv5.pc, 4 ships opencv4.pc).
pkg_check_modules(OPENCV opencv4)
if(NOT OPENCV_FOUND)
    pkg_check_modules(OPENCV opencv5)
endif()

# Find GLib/GIO for D-Bus (notifications, IPC)
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

# Generate virtual-keyboard CLIENT protocol (self-injection keyboard;
# XML lives in tests/runtime - it's the unstable upstream protocol)
execute_process(
    COMMAND ${WAYLAND_SCANNER} client-header
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/virtual-keyboard-unstable-v1.xml
    ${PROTOCOL_HEADERS_DIR}/virtual-keyboard-unstable-v1-client-protocol.h
)

execute_process(
    COMMAND ${WAYLAND_SCANNER} private-code
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/virtual-keyboard-unstable-v1.xml
    ${PROTOCOL_HEADERS_DIR}/virtual-keyboard-unstable-v1-protocol.c
)
# (compiled into the WM via PROTOCOL_SOURCES below - a stray list(APPEND)
# here was dead code, overwritten by the later set())

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
# virtual-keyboard-unstable-v1: the bridge's self-injection keyboard is a
# wayland CLIENT - its protocol code must build into the WM itself, not
# ride on the monorepo's wayland-protos archive (undefined refs without
# the embed - observed in the no-embed link)
set(PROTOCOL_SOURCES
    ${PROTOCOL_HEADERS_DIR}/xdg-shell-protocol.c
    ${PROTOCOL_HEADERS_DIR}/wlr-layer-shell-unstable-v1-protocol.c
    ${PROTOCOL_HEADERS_DIR}/text-input-unstable-v3-protocol.c
    ${PROTOCOL_HEADERS_DIR}/virtual-keyboard-unstable-v1-protocol.c
)

# C++ WM core sources
set(WM_CORE_SOURCES
    src/wm/core/View.cpp
    src/wm/core/Workspace.cpp
    src/wm/core/Layout.cpp
    src/wm/core/Focus.cpp
    src/wm/core/Server.cpp
    src/wm/core/DisplaySettings.cpp
    src/wm/core/bridge.cpp
    src/wm/core/Animation.cpp
    src/wm/core/Animator.cpp
    src/wm/core/NotificationDaemon.cpp
    src/wm/core/NotificationDaemon.hpp
    src/wm/core/WindowGroupManager.cpp
    src/wm/core/WindowGroupManager.hpp
    src/wm/core/Window.cpp
    src/wm/core/Window.hpp
    src/wm/core/CoreWindowManager.cpp
    src/wm/core/CoreWindowManager.hpp
    src/wm/scene/SceneGraph.hpp
    src/wm/scene/SceneGraph.cpp
    src/wm/scene/SceneGraphOps.cpp
    # XWaylandDnD - WIP, needs wlroots integration
    # src/wm/core/XWaylandDnD.cpp
    # src/wm/core/XWaylandDnD.hpp
    src/wm/core/LoadingScreen.c
    src/wm/core/LoadingScreen.h
    src/ui/font/Font.hpp
    src/ui/font/Font.cpp
    src/ui/lock/ScreenLock.hpp
    src/ui/lock/ScreenLock.cpp
    src/wm/render/render_c_wrappers.c
    src/wm/render/OverlayRenderer.cpp
    src/wm/render/OverlayRenderer.hpp
    src/wm/render/AppIconLoader.cpp
    src/wm/input/KeybindingManager.cpp
    src/wm/input/KeybindingManager.hpp
    src/wm/input/TextInputManager.cpp
    src/wm/desktop/DesktopManager.cpp
    src/wm/desktop/DesktopManager.hpp
    src/wm/desktop/NASAWallpaper.cpp
    src/wm/desktop/NASAWallpaper.hpp
    src/wm/desktop/NASAWallpaperTests.cpp
    src/wm/desktop/FileManager.cpp
    src/wm/desktop/FileManager.hpp
    src/wm/plugins/PluginManager.cpp
    src/wm/plugins/Plugin.cpp
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
    src/wm/plugins/ZoomPlugin.cpp
    src/wm/plugins/WorkspaceInfoBar.cpp
)

# Scene graph test executable
add_executable(test-scene-graph
    src/wm/scene/SceneGraphTest.cpp
    src/wm/scene/SceneGraph.cpp
    src/utils/Logger.c
)
target_include_directories(test-scene-graph PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)
target_link_libraries(test-scene-graph PRIVATE m)

# Keybinding parser test executable
add_executable(test-keybinding tests/test_keybinding.cpp)
target_include_directories(test-keybinding PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm/include
)
target_link_libraries(test-keybinding PRIVATE xkbcommon)

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
# Modular backend structure - C for performance-critical wlroots integration
set(WLR_BRIDGE_SOURCES
    wlr_bridge.c
)

add_library(wlr_bridge STATIC ${WLR_BRIDGE_SOURCES})
target_include_directories(wlr_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm/backend
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/include
    ${PROTOCOL_HEADERS_DIR}
)
target_include_directories(wlr_bridge PRIVATE
    ${WLROOTS_INCLUDE_DIRS}
    ${PROTOCOL_HEADERS_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)
target_compile_definitions(wlr_bridge PRIVATE WLR_USE_UNSTABLE)
target_link_libraries(wlr_bridge
    ${WLROOTS_LIBRARIES}
    dl
)
set_target_properties(wlr_bridge PROPERTIES
    LINKER_LANGUAGE C
)

# Add executable
# Havel language embedding (scripts as compositor commands). Detection
# runs before add_executable so the embed-only sources can join
# WM_CORE_SOURCES: HavelHost.cpp includes the monorepo's capi headers,
# so it only builds when the language actually links.
option(ENABLE_HAVEL_LANG "Embed the Havel language (config.hv, havel_run IPC)" ON)

set(HAVEL_LANG_LIB "")
set(HAVEL_CORE_LIB "")
if(ENABLE_HAVEL_LANG)
    # HAVEL_ARCHIVE_DIR selects which monorepo build tree provides the
    # archives. The shared build-debug is contested ground (parallel
    # sessions flip ENABLE_QT on and off; a Qt-enabled core references
    # services whose implementation lives in the Qt-only gui target, so
    # core+lang alone cannot resolve). A private out-of-source tree
    # (e.g. build-wm-embed configured with ENABLE_QT=OFF) stays stable.
    set(HAVEL_ARCHIVE_DIR "" CACHE PATH
        "Monorepo build dir providing libhavel_lang.a/libhavel_core.a (default: search)")
    foreach(_cand
            "${HAVEL_ARCHIVE_DIR}/libhavel_lang.a"
            "${CMAKE_CURRENT_SOURCE_DIR}/../build-debug/libhavel_lang.a"
            "${CMAKE_CURRENT_SOURCE_DIR}/../build-release/libhavel_lang.a"
            "${CMAKE_CURRENT_SOURCE_DIR}/../build/libhavel_lang.a")
        if(EXISTS "${_cand}")
            set(HAVEL_LANG_LIB "${_cand}")
            break()
        endif()
    endforeach()
    if(HAVEL_LANG_LIB)
        get_filename_component(_lang_dir "${HAVEL_LANG_LIB}" DIRECTORY)
        # libhavel_core.a carries the host-bridge implementations
        # (ModularHostBridges) and the app-layer backends they call
        # (IO, HotkeyManager, EventListener, DisplayManager, MouseService,
        # KeyMap) - the lang archive references their symbols.
        if(EXISTS "${_lang_dir}/libhavel_core.a")
            set(HAVEL_CORE_LIB "${_lang_dir}/libhavel_core.a")
        endif()
    endif()
endif()

if(HAVEL_LANG_LIB)
    list(APPEND WM_CORE_SOURCES
        src/wm/havel/HavelHost.cpp
        src/wm/havel/HavelHost.hpp
        src/wm/havel/HavelLangStubs.cpp
    )
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wm/backend
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${PROTOCOL_HEADERS_DIR}
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${XKB_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

# Enable unstable wlroots features
target_compile_definitions(${PROJECT_NAME} PRIVATE WLR_USE_UNSTABLE)

# ============================================================================
# Havel language embedding (scripts as compositor commands)
# ============================================================================
# Links the havel-lang static library from the sibling monorepo build.
# The monorepo builds it LLVM-free (its LLVM TUs are excluded), so no
# LLVM libraries are needed here. Undefined externals in the archive:
# libc/libm/libdl/readline/X11 only.

if(HAVEL_LANG_LIB)
    message(STATUS "Havel language embedding: ${HAVEL_LANG_LIB}")
    if(HAVEL_CORE_LIB)
        message(STATUS "Havel host bridges: ${HAVEL_CORE_LIB}")
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_HAVEL_LANG)
    # allow-multiple-definition: libhavel_core.a carries the monorepo's
    # own havel::WindowManager (X11-based) - same class name as this WM's
    # shell WindowManager. The archive member gets pulled for its 40
    # non-overlapping symbols; the 15 overlapping ones then collide.
    # The WM's objects link first, so with this flag the WM's
    # implementations win and the archive's duplicates are ignored. Safe
    # because the bridge code that would call the wrong instance never
    # runs: nothing in the WM calls createHostBridge().
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--allow-multiple-definition)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/havel-lang/capi"
    )
    # Both archives in ONE group: core's EventBridge references
    # EventRuntime (lang archive) - separate groups left it unresolved
    # (the lang archive was scanned before the reference existed).
    target_link_libraries(${PROJECT_NAME} PRIVATE
        -Wl,--start-group
        "${HAVEL_LANG_LIB}"
        "${HAVEL_CORE_LIB}"
        pthread
        dl
        m
        readline
        X11
        Xrandr
        Xinerama
        Xext
        Xi
        Xtst
        ffi
        wayland-client
        dbus-1
        asound
        -Wl,--end-group
    )
    # cv:: symbols only appear when the linked archives were built with
    # HAVE_OPENCV; absent that, linking opencv would be dead weight (and
    # fails outright on systems without the libs installed). Link only
    # the modules the monorepo's own link uses (core/imgproc/imgcodecs) -
    # ${OPENCV_LIBRARIES} wholesale drags in opencv_viz and its VTK
    # dependency (351 unresolved vtk:: symbols - observed).
    if(OPENCV_FOUND)
        foreach(_cvlib opencv_core opencv_imgproc opencv_imgcodecs)
            if(";${OPENCV_LIBRARIES};" MATCHES ";${_cvlib};")
                target_link_libraries(${PROJECT_NAME} PRIVATE ${_cvlib})
            endif()
        endforeach()
    endif()
    # Same conditional logic for OCR: archives built with HAVE_TESSERACT
    # reference tesseract::/leptonica symbols (the monorepo's own link
    # carries -ltesseract -lleptonica).
    pkg_check_modules(TESSERACT tesseract)
    pkg_check_modules(LEPTONICA leptonica)
    if(TESSERACT_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE tesseract)
    endif()
    if(LEPTONICA_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE leptonica)
    endif()
    # The monorepo's lang archive embeds the ORC JIT when its tree is
    # configured with ENABLE_LLVM=ON; pulled archive members reference
    # llvm:: symbols (847 unresolved refs - observed). Mirror the
    # monorepo's own link: llvm-config --libs all + --system-libs.
    find_program(LLVM_CONFIG_EXE llvm-config)
    if(LLVM_CONFIG_EXE)
        execute_process(COMMAND ${LLVM_CONFIG_EXE} --libdir
            OUTPUT_VARIABLE _llvm_libdir OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${LLVM_CONFIG_EXE} --libs all
            OUTPUT_VARIABLE _llvm_libs OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${LLVM_CONFIG_EXE} --system-libs
            OUTPUT_VARIABLE _llvm_syslibs OUTPUT_STRIP_TRAILING_WHITESPACE)
        separate_arguments(_llvm_libs)
        separate_arguments(_llvm_syslibs)
        if(_llvm_libdir)
            target_link_directories(${PROJECT_NAME} PRIVATE ${_llvm_libdir})
        endif()
        target_link_libraries(${PROJECT_NAME} PRIVATE ${_llvm_libs} ${_llvm_syslibs})
    else()
        message(STATUS "llvm-config not found - archive LLVM refs, if any, stay unresolved")
    endif()
    # wayland client-protocol code (zxdg_output_v1_interface etc.) that
    # the bridge layer references
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../build-debug/libwayland-protos.a")
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../build-debug/libwayland-protos.a")
    endif()
else()
    if(ENABLE_HAVEL_LANG)
        message(STATUS "Havel language embedding: libhavel_lang.a not found - building without script support")
    endif()
endif()

# Link with wlroots bridge library and other libraries
# wayland-client: the bridge's persistent self-injection keyboard is a
# wayland CLIENT of the compositor's own socket (wl_display_connect) -
# needed whether or not the language embed links.
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
target_link_libraries(${PROJECT_NAME} PRIVATE
    wlr_bridge
    ${WAYLAND_SERVER_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
    ${XKB_LIBRARIES}
    ${PIXMAN_LIBRARIES}
    ${FMT_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${PAM_LIBRARIES}
    ${PNG_LIBRARIES}
    ${CURL_LIBRARIES}
    ${GLIB_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# Set library directories
target_link_directories(${PROJECT_NAME} PRIVATE
    ${WAYLAND_SERVER_LIBRARY_DIRS}
    ${WLROOTS_LIBRARY_DIRS}
    ${XKB_LIBRARY_DIRS}
    ${PIXMAN_LIBRARY_DIRS}
    ${FMT_LIBRARY_DIRS}
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
# Sanitizers are Debug-only (see also: build.sh modes). Toggled off via
# -DENABLE_ASAN=OFF: the sandbox's exec layer has been observed placing
# sanitizer-linked PIEs at 0x100000000 - inside ASan's fixed shadow range
# [0x7fff7000,0x10007fff7fff] - which aborts at ASan init before main
# (sanitizers issue #856, "shadow range interleaves"). Non-sanitized
# builds are placement-immune.
option(ENABLE_ASAN "Enable address+undefined sanitizers" ON)
if(ENABLE_ASAN)
    target_compile_options(${PROJECT_NAME}  PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
endif()
# Install targets
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Install session file
install(FILES havel-wm.desktop
    DESTINATION share/wayland-sessions
)

# REPL: plain stdio binary, no Qt dependency
add_subdirectory(src/shell/repl)
install(TARGETS havel-repl
    RUNTIME DESTINATION bin
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

    # REMOVED: Broken agent-generated apps that don't work
    # - gamelauncher (deleted)
    # - dictionary (deleted) 
    # - updater (deleted)
    # - videoplayer (deleted)

    # Build and install settings
    add_subdirectory(src/shell/settings)

    # Build and install lock screen
    add_subdirectory(src/shell/lockscreen)

    # Font module test: bitmap (generated BDF) + vector (installed TTF)
    # faces through the one FreeType path, plus honest-failure checks.
    add_executable(test-font tests/test_font.cpp src/ui/font/Font.cpp)
    target_include_directories(test-font PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
        ${FREETYPE_INCLUDE_DIRS}
    )
    target_link_libraries(test-font PRIVATE ${FREETYPE_LIBRARIES})
    # Font.cpp uses the logger for its one-time face/failure reports.
    target_sources(test-font PRIVATE src/utils/Logger.c)

    # Build utilities test
    add_executable(test-utilities tests/test_utilities.c)
    target_include_directories(test-utilities PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    )
    target_link_libraries(test-utilities m)
endif()
