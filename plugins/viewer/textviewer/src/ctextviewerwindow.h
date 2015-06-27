#ifndef CTEXTVIEWERWINDOW_H
#define CTEXTVIEWERWINDOW_H

#include "plugininterface/cpluginwindow.h"
#include "cfinddialog.h"

namespace Ui {
class CTextViewerWindow;
}

class CTextViewerWindow : public CPluginWindow
{
	Q_OBJECT

public:
	explicit CTextViewerWindow(QWidget *parent = 0);
	~CTextViewerWindow();

	bool loadTextFile(const QString& file);

private slots:
	bool asDetectedAutomatically();
	bool asSystemDefault();
	bool asUtf8();
	bool asUtf16();
	bool asRichText();

	void find();
	void findNext();

private:
	bool readSource(QByteArray& data) const;

private:
	CFindDialog            _findDialog;
	QString                _sourceFilePath;
	Ui::CTextViewerWindow *ui;
};

#endif // CTEXTVIEWERWINDOW_H
