#ifndef CTEXTVIEWERWINDOW_H
#define CTEXTVIEWERWINDOW_H

#include "QtIncludes.h"
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
	void asDetectedAutomatically();
	void asSystemDefault();
	void asUtf8();
	void asUtf16();
	void asRichText();

	void find();
	void findNext();

private:
	QByteArray readSource() const;

private:
	CFindDialog            _findDialog;
	QString                _sourceFilePath;
	Ui::CTextViewerWindow *ui;
};

#endif // CTEXTVIEWERWINDOW_H
