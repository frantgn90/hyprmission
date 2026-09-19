#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>

inline HANDLE PHANDLE = nullptr;

// Set from the Hyprland config, e.g. in Lua:
//   hl.config({ plugin = { hyprmission = { live_previews = true, live_fps = 30 } } })
inline SP<Config::Values::CBoolValue> g_cfgLivePreviews;
inline SP<Config::Values::CIntValue>  g_cfgLiveFps;
