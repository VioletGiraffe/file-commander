#ifndef CVIEWERWINDOW_H
#define CVIEWERWINDOW_H

#include "plugin_export.h"
#include "utils/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

class PLUGIN_EXPORT CPluginWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit CPluginWindow(QWidget *parent = 0);

	bool autoDeleteOnClose() const;
	void setAutoDeleteOnClose(bool autoDelete);

protected:
	virtual void closeEvent(QCloseEvent* event) override;

private:
	bool _bAutoDeleteOnClose;
};

#endif // CVIEWERWINDOW_H
