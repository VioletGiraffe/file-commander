#pragma once

#include "cfilecommanderplugin.h"
#include "cfileviewer.h"


#include <memory>

class QMimeType;

// Well-known values returned by CFileCommanderViewerPlugin::category(). Free-form so third-party plugins may define their own; the app only special-cases Text (the omnivorous fallback viewer, and the Shift+F3 target).
namespace ViewerCategory {
	inline constexpr auto Text = "textviewer";
}

class PLUGIN_EXPORT CFileCommanderViewerPlugin : public CFileCommanderPlugin
{
public:
	[[nodiscard]] virtual bool canViewFile(const QString& fileName, const QMimeType& type) const = 0;
	// Empty until the host loads a file into it
	[[nodiscard]] virtual std::unique_ptr<CFileViewer> createViewer() = 0;
	// A free-form classification (see ViewerCategory) used to order viewer selection; the text viewer's category marks it as the omnivorous fallback.
	[[nodiscard]] virtual QString category() const = 0;

	[[nodiscard]] PluginType type() const override;
};
