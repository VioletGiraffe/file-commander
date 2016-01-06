#ifndef CTEXTVIEWERWINDOW_H
#define CTEXTVIEWERWINDOW_H

#include "plugininterface/cpluginwindow.h"
#include "cfinddialog.h"

#include "ui_ctextviewerwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QPlainTextEdit>
RESTORE_COMPILER_WARNINGS

class QLabel;

class CTextViewerWindow : public CPluginWindow, private Ui::CTextViewerWindow
{
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

	void encodingChanged(const QString& encoding, const QString& language = QString());

private:
	QPlainTextEdit _textBrowser;
	CFindDialog    _findDialog;
	QString        _sourceFilePath;

	QLabel       * _encodingLabel = nullptr;
};

#endif // CTEXTVIEWERWINDOW_H
