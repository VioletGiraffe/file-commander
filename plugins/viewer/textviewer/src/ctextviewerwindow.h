#ifndef CTEXTVIEWERWINDOW_H
#define CTEXTVIEWERWINDOW_H

#include "QtIncludes.h"
#include "cviewerwindow.h"

namespace Ui {
class CTextViewerWindow;
}

class CTextViewerWindow : public CViewerWindow
{
	Q_OBJECT

public:
	explicit CTextViewerWindow(QWidget *parent = 0);
	~CTextViewerWindow();

	bool loadTextFile(const QString& file);

private slots:
	void asAscii();
	void asUtf8();
	void asUtf16();
	void asRichText();

private:
	QByteArray readSource() const;

private:
	QString                _sourceFilePath;
	Ui::CTextViewerWindow *ui;
};

#endif // CTEXTVIEWERWINDOW_H
