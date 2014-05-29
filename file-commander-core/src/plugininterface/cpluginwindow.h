#ifndef CVIEWERWINDOW_H
#define CVIEWERWINDOW_H

#include "QtCoreIncludes"
#include "plugin_export.h"

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
