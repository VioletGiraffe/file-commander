#include "cfileviewer.h"

DISABLE_COMPILER_WARNINGS
#include <QAction>
#include <QContextMenuEvent>
#include <QFont>
#include <QLabel>
#include <QMenu>
RESTORE_COMPILER_WARNINGS

CFileViewer::CFileViewer(QWidget* parent) noexcept : QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
}

bool CFileViewer::loadFile(const QString& path)
{
	const QString error = tryLoadFile(path);
	if (error.isEmpty())
	{
		hideLoadError();
		return true;
	}

	showLoadError(error);
	return false;
}

void CFileViewer::addMenu(QMenu* menu)
{
	_menus.push_back(menu);

	// Shortcuts fire for actions of a focused widget, not of a popup
	for (QAction* action : menu->actions())
	{
		action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		addAction(action);
	}
}

void CFileViewer::showLoadError(const QString& message)
{
	if (!_errorLabel)
	{
		_errorLabel = new QLabel(this);
		_errorLabel->setAlignment(Qt::AlignCenter);
		_errorLabel->setWordWrap(true);
		_errorLabel->setMargin(20);
		_errorLabel->setStyleSheet(QStringLiteral("color: red; background-color: palette(window)")); // Opaque: covers the content
		QFont font = _errorLabel->font();
		font.setPointSizeF(font.pointSizeF() * 1.5);
		font.setBold(true);
		_errorLabel->setFont(font);
	}

	_errorLabel->setText(message);
	_errorLabel->setGeometry(rect());
	_errorLabel->raise();
	_errorLabel->show();
}

void CFileViewer::hideLoadError()
{
	if (_errorLabel)
		_errorLabel->hide();
}

void CFileViewer::contextMenuEvent(QContextMenuEvent* event)
{
	if (_menus.isEmpty())
		return;

	QMenu contextMenu;
	for (QMenu* menu : _menus)
		contextMenu.addMenu(menu);

	contextMenu.exec(event->globalPos());
}

void CFileViewer::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (_errorLabel)
		_errorLabel->setGeometry(rect());
}
