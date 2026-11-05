#pragma once

// Only for the plugin entry points: every module links its own static copy of the plugin interface classes, so nothing imports them
#if defined(_WIN32) && defined(PLUGIN_MODULE)
#  define PLUGIN_EXPORT __declspec(dllexport)
#else
#  define PLUGIN_EXPORT
#endif
