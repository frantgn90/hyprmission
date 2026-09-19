#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>

#include "Globals.hpp"
#include "Overview.hpp"

#include <string>

static SDispatchResult dispatchToggleOverview(std::string arg) {
    if (g_pOverview)
        g_pOverview->toggle();
    return SDispatchResult{};
}

static int luaToggleOverview(lua_State* L) {
    if (g_pOverview)
        g_pOverview->toggle();
    return 0;
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH)
        throw std::runtime_error("[hyprmission] Version mismatch: plugin was built against a different Hyprland than the one running.");

    g_pOverview = std::make_unique<COverview>();

    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprmission:toggle", dispatchToggleOverview);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprmission", "toggle", luaToggleOverview);

    return {"hyprmission", "macOS Mission Control-style workspace overview", "frantgn90", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_pOverview && g_pOverview->isOpen())
        g_pOverview->toggle();

    HyprlandAPI::removeLuaFunction(PHANDLE, "hyprmission", "toggle");
    HyprlandAPI::removeDispatcher(PHANDLE, "hyprmission:toggle");

    g_pOverview.reset();
}
