#ifndef CVIEWERWINDOW_H
#define CVIEWERWINDOW_H

#include "QtIncludes.h"
#include "plugin_export.h"

class PLUGIN_EXPORT CViewerWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit CViewerWindow(QWidget *parent = 0);

protected:
	virtual void closeEvent(QCloseEvent* event) override;

};

#endif // CVIEWERWINDOW_H
