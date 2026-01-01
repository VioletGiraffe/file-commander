#pragma once

#include "cfilecommanderplugin.h"
#include "cpluginwindow.h"

#include <memory>

class QMimeType;

class PLUGIN_EXPORT CFileCommanderViewerPlugin : public CFileCommanderPlugin
{
public:
	// The window needs a custom deleter because it must be deleted in the same dynamic library where it was allocated
	template <class T>
	struct WindowPtr final : public std::unique_ptr<T, void(*)(CPluginWindow*)>
	{
		using std::unique_ptr<T, void(*)(CPluginWindow*)>::unique_ptr;
		WindowPtr() noexcept : std::unique_ptr<T, void(*)(CPluginWindow*)>(nullptr, [](CPluginWindow*) {})
		{}

		template <typename... Args>
		static WindowPtr create(Args&&... args)
		{
			return WindowPtr(new T(std::forward<Args>(args)...), [](CPluginWindow* pluginWindow) {
				delete pluginWindow;
			});
		}
	};

	[[nodiscard]] virtual bool canViewFile(const QString& fileName, const QMimeType& type) const = 0;
	virtual WindowPtr<CPluginWindow> viewFile(const QString& fileName) = 0;

	PluginType type() override;
};
