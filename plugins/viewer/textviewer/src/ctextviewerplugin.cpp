#include "ctextviewerplugin.h"

#include "ctextviewer.h"
#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMimeType>
RESTORE_COMPILER_WARNINGS

uint32_t pluginInterfaceVersion()
{
	return PLUGIN_INTERFACE_VERSION;
}

CFileCommanderPlugin * createPlugin()
{
	DISABLE_COMPILER_WARNINGS
	Q_INIT_RESOURCE(icons);
	Q_INIT_RESOURCE(qutepart_syntax_files);
	Q_INIT_RESOURCE(qutepart_theme_data);
	RESTORE_COMPILER_WARNINGS

	return new CTextViewerPlugin;
}

bool CTextViewerPlugin::canViewFile(const QString& fileName, const QMimeType& /*type*/) const
{
	return CFileSystemObject(fileName).isFile();
}

std::unique_ptr<CFileViewer> CTextViewerPlugin::createViewer()
{
	return std::make_unique<CTextViewer>();
}

QString CTextViewerPlugin::name() const
{
	return QObject::tr("Plain text and HTML viewer plugin");
}

QString CTextViewerPlugin::category() const
{
	return ViewerCategory::Text;
}
