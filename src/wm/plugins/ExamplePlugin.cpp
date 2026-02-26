// Example plugin - demonstrates plugin system capabilities
#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>

namespace havel {

/**
 * Example plugin that logs compositor events
 * 
 * This demonstrates:
 * - Plugin lifecycle (init/fini)
 * - Event hooks (onOutputFrame, onViewMap, etc.)
 * - Using CompositorAPI to interact with compositor
 */
class ExamplePlugin : public Plugin {
public:
    const char* name() const override { return "example"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        printf("[ExamplePlugin] Initialized! Active workspace: %u\n", 
               m_api->getActiveWorkspace());
    }
    
    void fini() override {
        printf("[ExamplePlugin] Finalized\n");
        m_api = nullptr;
    }
    
    void onOutputFrame(const OutputFrameEvent& event) override {
        // Called on every frame - don't spam
        // printf("[ExamplePlugin] Frame: %dx%d\n", event.width, event.height);
    }
    
    void onViewMap(const ViewEvent& event) override {
        printf("[ExamplePlugin] View mapped: %s - %s (workspace %u)\n",
               event.appId ? event.appId : "unknown",
               event.title ? event.title : "untitled",
               event.workspace);
    }
    
    void onViewDestroy(const ViewEvent& event) override {
        printf("[ExamplePlugin] View destroyed: %s\n",
               event.title ? event.title : "unknown");
    }
    
    bool onKey(const KeyEvent& keyEvent) override {
        // Example: Consume Meta+X to switch workspace
        constexpr uint32_t MOD_LOGO = 1 << 6;

        if (keyEvent.pressed &&
            (keyEvent.modifiers & MOD_LOGO) &&
            keyEvent.keycode == 39) {  // X key

            printf("[ExamplePlugin] Meta+X consumed by plugin!\n");

            // Switch to next workspace
            uint32_t current = m_api->getActiveWorkspace();
            uint32_t next = (current + 1) % m_api->getWorkspaceCount();
            m_api->setActiveWorkspace(next);

            return true;  // Event consumed
        }

        return false;  // Event not consumed, pass to compositor
    }
    
private:
    CompositorAPI* m_api = nullptr;
};

// Plugin factory function - called by plugin manager
Plugin* create_example_plugin() {
    return new ExamplePlugin();
}

} // namespace havel
