#pragma once

#include <wm/plugins/Plugin.hpp>

namespace havel {

// Factory function for example plugin
// Note: Returns a Plugin* that must be deleted by the caller
Plugin* create_example_plugin();

} // namespace havel
