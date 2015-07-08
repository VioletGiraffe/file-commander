#include "ctextviewerwindow.h"
#include "ctextencodingdetector.h"

DISABLE_COMPILER_WARNINGS
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <assert.h>


CTextViewerWindow::CTextViewerWindow() :
	CPluginWindow(),
	_findDialog(this, "Plugins/TextViewer/Find/")
{
	setupUi(this);

	connect(actionOpen, &QAction::triggered, [this]() {
		const QString fileName = QFileDialog::getOpenFileName(this);
		if (!fileName.isEmpty())
			loadTextFile(fileName);
	});
	connect(actionReload, &QAction::triggered, [this]() {
		loadTextFile(_sourceFilePath);
	});
	connect(actionClose, SIGNAL(triggered()), SLOT(close()));

	connect(actionFind, &QAction::triggered, [this]() {
		_findDialog.exec();
	});
	connect(actionFind_next, SIGNAL(triggered()), SLOT(findNext()));

	connect(actionAuto_detect_encoding, SIGNAL(triggered()), SLOT(asDetectedAutomatically()));
	connect(actionSystemLocale, SIGNAL(triggered()), SLOT(asSystemDefault()));
	connect(actionUTF_8, SIGNAL(triggered()), SLOT(asUtf8()));
	connect(actionUTF_16, SIGNAL(triggered()), SLOT(asUtf16()));
	connect(actionHTML_RTF, SIGNAL(triggered()), SLOT(asRichText()));

	QActionGroup * group = new QActionGroup(this);
	group->addAction(actionSystemLocale);
	group->addAction(actionUTF_8);
	group->addAction(actionUTF_16);
	group->addAction(actionHTML_RTF);

	connect(&_findDialog, SIGNAL(find()), SLOT(find()));
	connect(&_findDialog, SIGNAL(findNext()), SLOT(findNext()));

	auto escScut = new QShortcut(QKeySequence("Esc"), this, SLOT(close()));
	connect(this, SIGNAL(destroyed()), escScut, SLOT(deleteLater()));
}

bool CTextViewerWindow::loadTextFile(const QString& file)
{
	setWindowTitle(file);
	_sourceFilePath = file;
	if (_sourceFilePath.endsWith(".htm", Qt::CaseInsensitive) || _sourceFilePath.endsWith(".html", Qt::CaseInsensitive) || _sourceFilePath.endsWith(".rtf", Qt::CaseInsensitive))
		return asRichText();
	else
		return asDetectedAutomatically();
}

bool CTextViewerWindow::asDetectedAutomatically()
{
	QTextCodec::ConverterState state;
	QTextCodec *codec = QTextCodec::codecForName("UTF-8");
	if (!codec)
		return false;

	QByteArray data;
	if (!readSource(data))
	{
		QMessageBox::warning(dynamic_cast<QWidget*>(parent()), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return false;
	}

	QString text(codec->toUnicode(data.constData(), data.size(), &state));
	if (state.invalidChars > 0)
	{
		text = CTextEncodingDetector::decode(data).first;
		if (!text.isEmpty())
			textBrowser.setPlainText(text);
		else
			return asSystemDefault();
	}
	else
	{
		actionUTF_8->setChecked(true);
		textBrowser.setPlainText(text);
	}

	return true;
}

bool CTextViewerWindow::asSystemDefault()
{
	QTextCodec * codec = QTextCodec::codecForLocale();
	if (!codec)
		return false;

	QByteArray data;
	if (!readSource(data))
	{
		QMessageBox::warning(dynamic_cast<QWidget*>(parent()), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return false;
	}

	textBrowser.setPlainText(codec->toUnicode(data));
	actionSystemLocale->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf8()
{
	QByteArray data;
	if (!readSource(data))
	{
		QMessageBox::warning(dynamic_cast<QWidget*>(parent()), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return false;
	}

	textBrowser.setPlainText(QString::fromUtf8(data));
	actionUTF_8->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf16()
{
	QByteArray data;
	if (!readSource(data))
	{
		QMessageBox::warning(dynamic_cast<QWidget*>(parent()), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return false;
	}

	textBrowser.setPlainText(QString::fromUtf16((const ushort*)data.constData()));
	actionUTF_16->setChecked(true);

	return true;
}

bool CTextViewerWindow::asRichText()
{
	textBrowser.setSource(QUrl::fromLocalFile(_sourceFilePath));
	actionHTML_RTF->setChecked(true);

	return true;
}

void CTextViewerWindow::find()
{
	textBrowser.moveCursor(_findDialog.searchBackwards() ? QTextCursor::End : QTextCursor::Start);
	findNext();
}

void CTextViewerWindow::findNext()
{
	const QString expression = _findDialog.searchExpression();
	if (expression.isEmpty())
		return;

	QTextDocument::FindFlags flags = 0;
	if (_findDialog.caseSensitive())
		flags |= QTextDocument::FindCaseSensitively;
	if (_findDialog.searchBackwards())
		flags |= QTextDocument::FindBackward;
	if (_findDialog.wholeWords())
		flags |= QTextDocument::FindWholeWords;

	bool found;
	const QTextCursor startCursor = textBrowser.textCursor();
#if  QT_VERSION >= QT_VERSION_CHECK(5,3,0)
	if (_findDialog.regex())
		found = textBrowser.find(QRegExp(_findDialog.searchExpression(), _findDialog.caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive), flags);
	else
#endif
		found = textBrowser.find(_findDialog.searchExpression(), flags);

	if(!found && (startCursor.isNull() || startCursor.position() == 0))
		QMessageBox::information(this, tr("Not found"), tr("Expression \"%1\" not found").arg(expression));
	else if (!found && startCursor.position() > 0)
	{
		if (QMessageBox::question(this, tr("Not found"), _findDialog.searchBackwards() ? tr("Beginning of file reached, do you want to restart search from the end?") : tr("End of file reached, do you want to restart search from the top?")) == QMessageBox::Yes)
			find();
	}
}

bool CTextViewerWindow::readSource(QByteArray& data) const
{
	QFile file(_sourceFilePath);
	if (file.exists() && file.open(QIODevice::ReadOnly))
	{
		data = file.readAll();
		return data.size() > 0 || file.size() == 0;
	}
	else
		return false;
}
