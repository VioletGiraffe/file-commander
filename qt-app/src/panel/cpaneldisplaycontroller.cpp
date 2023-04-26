#include "cpaneldisplaycontroller.h"
#include "cpanelwidget.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStackedWidget>
RESTORE_COMPILER_WARNINGS

void CPanelDisplayController::setPanelStackedWidget(QStackedWidget* widget)
{
	_panelStackedWidget = widget;
}

void CPanelDisplayController::setPanelWidget(CPanelWidget* panelWidget)
{
	assert_and_return_r(panelWidget, );
	assert_and_return_r(_panelStackedWidget, );

	// Removing the old widget, if any
	if (_panelWidget)
		_panelStackedWidget->removeWidget(_panelWidget);

	_panelWidget = panelWidget;

	_panelStackedWidget->insertWidget(0, panelWidget);
	if (!_quickViewActive)
		_panelStackedWidget->setCurrentIndex(0);

	// Sanity check
	assert_r((!_quickViewActive && _panelStackedWidget->count() == 1) || (_quickViewActive && _panelStackedWidget->count() == 2));
}

CPanelWidget* CPanelDisplayController::panelWidget() const
{
	return _panelWidget;
}

void CPanelDisplayController::startQuickView(CFileCommanderViewerPlugin::PluginWindowPointerType&& viewerWindow)
{
	if (!viewerWindow)
		return;

	// Sanity check
	assert_r((!_quickViewActive && !_quickViewWindow) || (quickViewActive() && _quickViewWindow));

	if (_quickViewActive)
		endQuickView();

	_quickViewWindow = std::move(viewerWindow);
	_quickViewActive = _quickViewWindow ? true : false;
	// Sanity check
	assert_and_return_r(_panelStackedWidget && _panelStackedWidget->count() == 1, );
	_panelStackedWidget->addWidget(_quickViewWindow->centralWidget());
	_panelStackedWidget->setCurrentIndex(1);
}

void CPanelDisplayController::endQuickView()
{
	// Sanity check
	assert_r((!_quickViewActive && !_quickViewWindow) || (quickViewActive() && _quickViewWindow));

	if (!_quickViewActive)
		return;

	// Sanity check
	assert_and_return_r(_panelStackedWidget && _panelStackedWidget->count() == 2, );
	_panelStackedWidget->removeWidget(_panelStackedWidget->widget(1));

	_quickViewActive = false;
	_quickViewWindow.reset();
}

bool CPanelDisplayController::quickViewActive() const
{
	return _quickViewActive;
}
