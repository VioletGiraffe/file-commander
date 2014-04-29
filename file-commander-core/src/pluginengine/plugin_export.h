#pragma once

#ifdef _WIN32
#  ifdef PLUGIN_MODULE
#    define PLUGIN_EXPORT __declspec(dllexport)
#    define PLUGIN_EXPORT_ONLY __declspec(dllimport)
#  else
#    define PLUGIN_EXPORT __declspec(dllimport)
#    define PLUGIN_EXPORT_ONLY
#  endif
#else
#    define PLUGIN_EXPORT
#endif
