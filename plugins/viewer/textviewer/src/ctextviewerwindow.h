#ifndef CTEXTVIEWERWINDOW_H
#define CTEXTVIEWERWINDOW_H

#include "plugininterface/cpluginwindow.h"
#include "cfinddialog.h"

#include "ui_ctextviewerwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QPlainTextEdit>
RESTORE_COMPILER_WARNINGS

class CTextViewerWindow : public CPluginWindow, private Ui::CTextViewerWindow
{
	Q_OBJECT

public:
	CTextViewerWindow();

	bool loadTextFile(const QString& file);

private:
	bool asDetectedAutomatically();
	bool asSystemDefault();
	bool asUtf8();
	bool asUtf16();
	bool asRichText();

	void find();
	void findNext();

	bool readSource(QByteArray& data) const;

private:
	QPlainTextEdit	_textBrowser;
	CFindDialog		_findDialog;
	QString			_sourceFilePath;
};

#endif // CTEXTVIEWERWINDOW_H
