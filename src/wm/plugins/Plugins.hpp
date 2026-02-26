#pragma once

#include <wm/plugins/Plugin.hpp>

namespace havel {

// Plugin factory functions
Plugin* create_example_plugin();
Plugin* create_blur_plugin();
Plugin* create_scale_plugin();
Plugin* create_wallpaper_plugin();
Plugin* create_notifications_plugin();
Plugin* create_custom_layouts_plugin();

} // namespace havel
