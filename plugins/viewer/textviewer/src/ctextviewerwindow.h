#ifndef CTEXTVIEWERWINDOW_H
#define CTEXTVIEWERWINDOW_H

#include "plugininterface/cpluginwindow.h"
#include "cfinddialog.h"

#include "ui_ctextviewerwindow.h"

#include <QPlainTextEdit>

class CTextViewerWindow : public CPluginWindow, private Ui::CTextViewerWindow
{
	Q_OBJECT

public:
	CTextViewerWindow();

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
	QPlainTextEdit	_textBrowser;
	CFindDialog		_findDialog;
	QString			_sourceFilePath;
};

#endif // CTEXTVIEWERWINDOW_H
