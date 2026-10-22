#include "cviewerwindow.h"

#include "plugininterface/cfileviewer.h"


// Submodule includes
#include "widgets/cpersistenceenabler.h"


DISABLE_COMPILER_WARNINGS
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
RESTORE_COMPILER_WARNINGS

CViewerWindow::CViewerWindow(std::unique_ptr<CFileViewer> viewer, const QString& viewerCategory) :
	_viewer(viewer.release())
{
	setAttribute(Qt::WA_DeleteOnClose);
	setCentralWidget(_viewer);

	setWindowIcon(_viewer->windowIcon());
	connect(_viewer, &QWidget::windowIconChanged, this, &QWidget::setWindowIcon);

	QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
	fileMenu->addAction(tr("&Open..."), QKeySequence{ Qt::CTRL | Qt::Key_O }, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this);
		if (!path.isEmpty())
			loadFile(path);
	});
	fileMenu->addAction(tr("&Reload"), QKeySequence{ Qt::Key_F5 }, this, [this] { _viewer->loadFile(_filePath); });
	fileMenu->addSeparator();
	fileMenu->addAction(tr("&Close"), this, &QWidget::close);

	for (QMenu* menu : _viewer->menus())
		menuBar()->addMenu(menu);

	new QShortcut(QKeySequence{ Qt::Key_Escape }, this, this, &QWidget::close);
	_viewer->setFocus(); // The viewer's shortcuts are scoped to it

	// A window nothing has resized takes its sizeHint() on the first show, so sizing to the content needs no code here
	if (!_viewer->sizesToContent())
		enablePersistence(this, QStringLiteral("Plugins/%1/Window").arg(viewerCategory), CPersistenceEnabler::Delayed{ false });
}

void CViewerWindow::loadFile(const QString& path)
{
	_viewer->loadFile(path);
	_filePath = path;

	const QFileInfo fi(path);
	setWindowTitle(fi.fileName() + " [" + fi.absolutePath() + "]");
	setWindowFilePath(path); // macOS only: the title bar's document proxy icon
}
