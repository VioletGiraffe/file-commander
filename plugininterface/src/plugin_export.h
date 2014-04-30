#pragma once

#ifdef _WIN32
#  ifdef PLUGIN_MODULE
#    define PLUGIN_EXPORT __declspec(dllexport)
#  else
#    define PLUGIN_EXPORT __declspec(dllimport)
#  endif
#else
#    define PLUGIN_EXPORT
#endif
