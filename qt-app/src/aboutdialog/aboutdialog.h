#pragma once

#include "pluginengine/cpluginengine.h"


#include <vector>

class QWidget;

void showAboutDialog(QWidget* parent, const std::vector<CPluginEngine::PluginInfo>& activePlugins);
