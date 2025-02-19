#include "cmainwindow.h"
#include "settings/csettings.h"
#include "system/win_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QStyleHints>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <string_view>

int main(int argc, char *argv[])
{
	AdvancedAssert::setLoggingFunc([](const char* message){
		qInfo() << message;
	});

	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	qInfo().nospace() << "Built with Qt " << QT_VERSION_STR << ", running with Qt " << qVersion();
	assert_r(std::string_view{QT_VERSION_STR} == qVersion());

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
	QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
	QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

	QApplication app(argc, argv);
	app.setOrganizationName("GitHubSoft");
	app.setApplicationName("File Commander");

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0) && defined(_WIN32)
	if (auto* styleHints = app.styleHints(); styleHints && styleHints->colorScheme() == Qt::ColorScheme::Dark)
	{
		// Fix the terrible default alternate row color on Windows
		QPalette p = QApplication::palette();
		QColor color = p.color(QPalette::Base);
		color = color.lighter(115);
		p.setColor(QPalette::AlternateBase, color);
		QApplication::setPalette(p);
	}
#endif

	QFontDatabase::addApplicationFont(":/fonts/Roboto Mono.ttf");

	{
		QFont font = QApplication::font();
		font.setPointSizeF(font.pointSizeF() + 1.0);
		QApplication::setFont(font);
	}

	if (const auto styleSheet = CSettings{}.value("Interface/Style/StylesheetText").toString(); !styleSheet.isEmpty())
		qApp->setStyleSheet(styleSheet);

	CMainWindow w;
	w.updateInterface();

	if (app.arguments().contains("--test-launch"))
		QTimer::singleShot(5000, [] { qApp->quit(); });

	return app.exec();
}

