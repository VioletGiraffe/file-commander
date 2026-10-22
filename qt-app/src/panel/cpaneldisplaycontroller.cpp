#include "cpaneldisplaycontroller.h"

#include "cpanelwidget.h"
#include "plugininterface/cfileviewer.h"


// Submodule includes
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
	if (!quickViewActive())
		_panelStackedWidget->setCurrentIndex(0);

	// Sanity check
	assert_r(_panelStackedWidget->count() == (quickViewActive() ? 2 : 1));
}

CPanelWidget* CPanelDisplayController::panelWidget() const
{
	return _panelWidget;
}

void CPanelDisplayController::showQuickView(CFileCommanderViewerPlugin& plugin, const QString& filePath)
{
	if (_quickViewPlugin != &plugin)
	{
		endQuickView();

		assert_and_return_r(_panelStackedWidget && _panelStackedWidget->count() == 1, );
		_quickViewer = plugin.createViewer().release(); // The stacked widget owns it from here
		_quickViewer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); // Keeps the panel's size hint independent of the content
		_panelStackedWidget->addWidget(_quickViewer);
		_panelStackedWidget->setCurrentIndex(1);
		_quickViewPlugin = &plugin;
	}

	_quickViewer->loadFile(filePath);
	_quickViewFilePath = filePath;
}

void CPanelDisplayController::endQuickView()
{
	if (!quickViewActive())
		return;

	assert_and_return_r(_panelStackedWidget && _panelStackedWidget->count() == 2, );
	delete _quickViewer; // The stacked widget drops a deleted child
	_quickViewer = nullptr;
	_quickViewPlugin = nullptr;
	_quickViewFilePath.clear();
	_panelStackedWidget->setCurrentIndex(0);
}
