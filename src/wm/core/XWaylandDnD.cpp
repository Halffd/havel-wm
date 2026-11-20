// XWayland Drag and Drop Implementation

#include "XWaylandDnD.hpp"
#include <wm/Server.hpp>
#include <Logger.h>
#include <algorithm>
#include <cstring>

namespace havel {

static DragDropState g_dndState;

void initXWaylandDnD(struct wlr_xwayland* xwayland) {
    if (!xwayland) return;
    
    LOG_INFO("[XWaylandDnD] Initialized");
}

bool handleXWaylandDnDEvent(struct wlr_xwayland* xwayland, xcb_generic_event_t* event) {
    if (!xwayland || !event) return false;
    
    uint8_t response_type = event->response_type & ~0x80;
    
    // Handle Xdnd (X Drag and Drop) protocol events
    if (response_type == XCB_CLIENT_MESSAGE) {
        xcb_client_message_event_t* cm = (xcb_client_message_event_t*)event;
        
        // XdndEnter
        if (cm->type == xwayland->atoms[XCBC_ATOM_XDND_ENTER]) {
            g_dndState.dragging = true;
            g_dndState.xdndSource = cm->data.data32[0];
            g_dndState.xdndVersion = cm->data.data32[1] >> 24;
            
            LOG_DEBUG("[XWaylandDnD] XdndEnter from window %u, version %d",
                     g_dndState.xdndSource, g_dndState.xdndVersion);
            return true;
        }
        
        // XdndPosition
        if (cm->type == xwayland->atoms[XCBC_ATOM_XDND_POSITION]) {
            g_dndState.xdndX = cm->data.data32[2] >> 16;
            g_dndState.xdndY = cm->data.data32[2] & 0xFFFF;
            g_dndState.xdndTarget = cm->data.data32[0];
            
            // Send XdndStatus reply
            xcb_client_message_event_t status;
            memset(&status, 0, sizeof(status));
            status.response_type = XCB_CLIENT_MESSAGE;
            status.window = g_dndState.xdndSource;
            status.type = xwayland->atoms[XCBC_ATOM_XDND_STATUS];
            status.data.data32[0] = g_dndState.xdndTarget;
            status.data.data32[1] = 1;  // Accept
            status.data.data32[2] = 0;  // No rectangle
            status.data.data32[3] = 0;  // No rectangle
            status.data.data32[4] = xwayland->atoms[XCBC_ATOM_XDND_ACTION_COPY];
            
            xcb_send_event(xwayland->xcb_conn, 0, g_dndState.xdndSource,
                          XCB_EVENT_MASK_NO_EVENT, (char*)&status);
            xcb_flush(xwayland->xcb_conn);
            
            LOG_DEBUG("[XWaylandDnD] XdndPosition at (%d, %d)",
                     g_dndState.xdndX, g_dndState.xdndY);
            return true;
        }
        
        // XdndDrop
        if (cm->type == xwayland->atoms[XCBC_ATOM_XDND_DROP]) {
            // Request the actual data
            xcb_convert_selection(xwayland->xcb_conn,
                                 g_dndState.xdndTarget,
                                 xwayland->atoms[XCBC_ATOM_XDND_SELECTION],
                                 xwayland->atoms[XCBC_ATOM_XDND_DATA],
                                 xwayland->atoms[XCBC_ATOM_XDND_DATA],
                                 XCB_CURRENT_TIME);
            
            LOG_DEBUG("[XWaylandDnD] XdndDrop");
            return true;
        }
        
        // XdndLeave
        if (cm->type == xwayland->atoms[XCBC_ATOM_XDND_LEAVE]) {
            g_dndState.dragging = false;
            g_dndState.uris.clear();
            g_dndState.text.clear();
            
            LOG_DEBUG("[XWaylandDnD] XdndLeave");
            return true;
        }
    }
    
    // Handle SelectionNotify (data received)
    if (response_type == XCB_SELECTION_NOTIFY) {
        xcb_selection_notify_event_t* sn = (xcb_selection_notify_event_t*)event;
        
        if (sn->property == xwayland->atoms[XCBC_ATOM_XDND_DATA]) {
            // Read the data
            xcb_get_property_reply_t* reply = xcb_get_property_reply(
                xwayland->xcb_conn, sn->property, NULL);
            
            if (reply) {
                int len = xcb_get_property_value_length(reply);
                char* data = (char*)xcb_get_property_value(reply);
                
                // Parse URI list
                if (len > 0) {
                    std::string uriData(data, len);
                    std::istringstream iss(uriData);
                    std::string line;
                    
                    while (std::getline(iss, line)) {
                        if (!line.empty() && line[0] != '#') {
                            // Remove trailing \r\n
                            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                                line.pop_back();
                            }
                            if (!line.empty()) {
                                g_dndState.uris.push_back(line);
                            }
                        }
                    }
                    
                    g_dndState.dataType = DnDType::TextUriList;
                    LOG_INFO("[XWaylandDnD] Received %zu URIs", g_dndState.uris.size());
                }
                
                free(reply);
            }
            
            // Send XdndFinished
            xcb_client_message_event_t finished;
            memset(&finished, 0, sizeof(finished));
            finished.response_type = XCB_CLIENT_MESSAGE;
            finished.window = g_dndState.xdndSource;
            finished.type = xwayland->atoms[XCBC_ATOM_XDND_FINISHED];
            finished.data.data32[0] = g_dndState.xdndTarget;
            finished.data.data32[1] = 1;  // Success
            
            xcb_send_event(xwayland->xcb_conn, 0, g_dndState.xdndSource,
                          XCB_EVENT_MASK_NO_EVENT, (char*)&finished);
            xcb_flush(xwayland->xcb_conn);
            
            return true;
        }
    }
    
    return false;
}

DragDropState* getDragDropState() {
    return &g_dndState;
}

void clearDragDropState() {
    g_dndState.dragging = false;
    g_dndState.uris.clear();
    g_dndState.text.clear();
    g_dndState.dataType = DnDType::None;
}

bool isDraggingFiles() {
    return g_dndState.dragging && g_dndState.dataType == DnDType::TextUriList;
}

const std::vector<std::string>& getDraggedUris() {
    return g_dndState.uris;
}

void handleTaskbarDrop(int x, int y, const std::vector<std::string>& uris) {
    if (uris.empty()) return;
    
    LOG_INFO("[XWaylandDnD] Drop on taskbar at (%d, %d) with %zu URIs",
             x, y, uris.size());
    
    // Process each URI
    for (const auto& uri : uris) {
        LOG_INFO("[XWaylandDnD] URI: %s", uri.c_str());
        
        // Check if it's a .desktop file
        if (uri.find(".desktop") != std::string::npos) {
            // Would pin to taskbar
            LOG_INFO("[XWaylandDnD] Would pin: %s", uri.c_str());
        }
    }
}

} // namespace havel
