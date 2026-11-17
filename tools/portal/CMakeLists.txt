# havel-portal: xdg-desktop-portal backend for Havel WM.
#
# Standalone process - the compositor must not own the portal DBus name
# itself (the running xdg-desktop-portal dispatches per-desktop .portal
# files and would race it). Links the staging ext-image-copy-capture
# protocols as a wayland CLIENT.

set(HAVEL_PORTAL_SOURCES
    havel-portal.cpp
    capture.cpp
    screencast.cpp
)

# Generate the protocol glue from the wayland-protocols staging XMLs
set(PORTAL_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/portal-protocols)
file(MAKE_DIRECTORY ${PORTAL_PROTO_DIR})

set(PORTAL_XMLS
    ${WAYLAND_PROTOCOLS_DIR}/staging/ext-image-capture-source/ext-image-capture-source-v1.xml
    ${WAYLAND_PROTOCOLS_DIR}/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
    # ext-image-capture-source references the foreign-toplevel handle
    # interface (toplevel capture sources); we only use output sources but
    # the glue links the symbols.
    ${WAYLAND_PROTOCOLS_DIR}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
)

foreach(_xml ${PORTAL_XMLS})
    get_filename_component(_base ${_xml} NAME_WE)
    execute_process(
        COMMAND ${WAYLAND_SCANNER} private-code ${_xml} ${PORTAL_PROTO_DIR}/${_base}-protocol.c
        RESULT_VARIABLE _scan_rc
    )
    if(NOT _scan_rc EQUAL 0)
        message(WARNING "wayland-scanner failed on ${_xml}: ${_scan_rc}")
    endif()
    execute_process(
        COMMAND ${WAYLAND_SCANNER} client-header ${_xml} ${PORTAL_PROTO_DIR}/${_base}-client-protocol.h
        RESULT_VARIABLE _scan_rc
    )
    if(NOT _scan_rc EQUAL 0)
        message(WARNING "wayland-scanner failed on ${_xml}: ${_scan_rc}")
    endif()
    list(APPEND HAVEL_PORTAL_SOURCES ${PORTAL_PROTO_DIR}/${_base}-protocol.c)
endforeach()

add_executable(havel-portal ${HAVEL_PORTAL_SOURCES})
target_include_directories(havel-portal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PORTAL_PROTO_DIR}
)
target_link_libraries(havel-portal PRIVATE
    ${PNG_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
)
target_include_directories(havel-portal SYSTEM PRIVATE
    ${PNG_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
)
set_target_properties(havel-portal PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Portal dispatch file: tells xdg-desktop-portal which interfaces this
# backend serves. Installed next to the other .portal files so the
# running portal picks the backend up on the next request.
set(PORTAL_DESKTOP_DIR ${CMAKE_INSTALL_PREFIX}/share/xdg-desktop-portal/portals)
# Interfaces lists the IMPL interface names (org.freedesktop.impl.portal.*),
# not the frontend portal names: xdg-desktop-portal rejects a .portal file
# naming the frontend ("Not a portal backend interface", observed).
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/havel.portal
"[portal]
DBusName=org.freedesktop.impl.portal.desktop.havel
Interfaces=org.freedesktop.impl.portal.Screenshot;org.freedesktop.impl.portal.ScreenCast
UseIn=havel;Havel;havel-wm
")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/havel.portal
        DESTINATION ${PORTAL_DESKTOP_DIR})
