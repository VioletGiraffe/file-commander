#include "aboutdialog.h"
#include "../version.h"

#include "appdialogs/caboutdialog.h"

#include "3rdparty/magic_enum/magic_enum.hpp"

DISABLE_COMPILER_WARNINGS
#include <QAbstractItemView>
#include <QFontMetrics>
#include <QLabel>
#include <QListWidget>
#include <QObject>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <string_view>

namespace {

constexpr struct {
	const char* what;
	const char* project;
	const char* url;
} ACKNOWLEDGEMENTS[] = {
	{ "Syntax highlighter",           "Qutepart by diegoiast",       "https://github.com/diegoiast/qutepart-cpp" },
	{ "Markdown parser",              "maddy by progsource",         "https://github.com/progsource/maddy" },
	{ "Hash map",                     "unordered_dense by martinus", "https://github.com/martinus/unordered_dense" },
	{ "Hash map",                     "Boost.Unordered by boostorg", "https://github.com/boostorg/unordered" },
	{ "Move-only function wrapper",   "function2 by Naios",          "https://github.com/Naios/function2" },
	{ "SIMD portability",             "SIMDe by simd-everywhere",    "https://github.com/simd-everywhere/simde" },
	{ "Unit testing framework",       "Catch2 by catchorg",          "https://github.com/catchorg/Catch2" },
	{ "Compile-time enum reflection", "magic_enum by Neargye",       "https://github.com/Neargye/magic_enum" },
	{ "Font",                         "Roboto Mono by googlefonts",  "https://github.com/googlefonts/RobotoMono" }
};

QString pluginTypeName(CFileCommanderPlugin::PluginType type)
{
	const std::string_view name = magic_enum::enum_name(type);
	return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

QString acknowledgementsText()
{
	QString text = "3rdparty acknowledgements:";
	for (const auto& [what, project, url] : ACKNOWLEDGEMENTS)
		text += QString{ "<br/>%1 - <a href=\"%3\">%2</a>" }.arg(what, project, url);

	return text;
}

}

void showAboutDialog(QWidget* parent, const std::vector<CPluginEngine::PluginInfo>& activePlugins)
{
	CAboutDialog dialog(VERSION_STRING, parent, "2013");

	QLabel* acknowledgements = new QLabel(acknowledgementsText(), &dialog);
	acknowledgements->setTextFormat(Qt::RichText);
	acknowledgements->setOpenExternalLinks(true);
	acknowledgements->setAlignment(Qt::AlignLeft | Qt::AlignTop);

	QListWidget* plugins = new QListWidget(&dialog);
	plugins->setSelectionMode(QAbstractItemView::NoSelection);

	// The Windows 11 style inflates CT_ItemViewItem height; padding cannot shrink it, only an explicit height can.
	// Requires NoSelection: a QSS-styled item view stops taking selection colors from the palette.
	const int rowHeight = plugins->fontMetrics().lineSpacing() + 2;
	plugins->setStyleSheet(QString{ "QListWidget::item { height: %1px; }" }.arg(rowHeight));

	constexpr int MIN_PLUGIN_ROWS = 5;
	plugins->setMinimumHeight(MIN_PLUGIN_ROWS * rowHeight + 2 * plugins->frameWidth());

	for (const CPluginEngine::PluginInfo& plugin: activePlugins)
	{
		QString item = plugin.name + " (" + pluginTypeName(plugin.type) + ")";
		if (!plugin.description.isEmpty())
			item += " - " + plugin.description;

		plugins->addItem(item);
	}

	QVBoxLayout& layout = dialog.customContentLayout();
	layout.addSpacing(20);
	layout.addWidget(acknowledgements);
	layout.addSpacing(30);
	layout.addWidget(new QLabel(QObject::tr("Active plugins:"), &dialog));
	layout.addWidget(plugins, 1);

	// Explicit size: the plugin list's own size hint would otherwise drive the dialog's dimensions.
	dialog.resize(387, 422);
	dialog.exec();
}
