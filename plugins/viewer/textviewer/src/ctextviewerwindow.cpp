#ifdef _WIN32
#pragma warning(push, 0) // set W0
#endif

#include "ctextviewerwindow.h"
#include "ui_ctextviewerwindow.h"

#include <QMessageBox>

#ifdef _WIN32
#pragma warning(pop) // set W0
#endif

CTextViewerWindow::CTextViewerWindow(QWidget *parent) :
	CPluginWindow(parent),
	_findDialog(this),
	ui(new Ui::CTextViewerWindow)
{
	ui->setupUi(this);

	connect(ui->actionOpen, &QAction::triggered, [this](){
		const QString fileName = QFileDialog::getOpenFileName(this);
		if (!fileName.isEmpty())
			loadTextFile(fileName);
	});
	connect(ui->actionReload, &QAction::triggered, [this](){
		loadTextFile(_sourceFilePath);
	});
	connect(ui->actionClose, SIGNAL(triggered()), SLOT(close()));

	connect(ui->actionFind, &QAction::triggered, [this](){
		_findDialog.exec();
	});
	connect(ui->actionFind_next, SIGNAL(triggered()), SLOT(findNext()));

	connect(ui->actionASCI, SIGNAL(triggered()), SLOT(asAscii()));
	connect(ui->actionUTF_8, SIGNAL(triggered()), SLOT(asUtf8()));
	connect(ui->actionUTF_16, SIGNAL(triggered()), SLOT(asUtf16()));
	connect(ui->actionHTML_RTF, SIGNAL(triggered()), SLOT(asRichText()));

	QActionGroup * group = new QActionGroup(this);
	group->addAction(ui->actionASCI);
	group->addAction(ui->actionUTF_8);
	group->addAction(ui->actionUTF_16);
	group->addAction(ui->actionHTML_RTF);

	connect(&_findDialog, SIGNAL(find()), SLOT(find()));
	connect(&_findDialog, SIGNAL(findNext()), SLOT(findNext()));
}

CTextViewerWindow::~CTextViewerWindow()
{
	delete ui;
}

bool CTextViewerWindow::loadTextFile(const QString& file)
{
	setWindowTitle(file);
	_sourceFilePath = file;
	if (_sourceFilePath.endsWith(".htm", Qt::CaseInsensitive) || _sourceFilePath.endsWith(".html", Qt::CaseInsensitive) || _sourceFilePath.endsWith(".rtf", Qt::CaseInsensitive))
		asRichText();
	else
		asUtf8();
	return true;
}

void CTextViewerWindow::asAscii()
{
	ui->textBrowser->setPlainText(QString::fromLatin1(readSource()));
	ui->actionASCI->setChecked(true);
}

void CTextViewerWindow::asUtf8()
{
	ui->textBrowser->setPlainText(QString::fromUtf8(readSource()));
	ui->actionUTF_8->setChecked(true);
}

void CTextViewerWindow::asUtf16()
{
	const QByteArray data(readSource());
	ui->textBrowser->setPlainText(QString::fromUtf16((const ushort*)data.data(), data.size()/2));
	ui->actionUTF_16->setChecked(true);
}

void CTextViewerWindow::asRichText()
{
	ui->textBrowser->setSource(QUrl::fromLocalFile(_sourceFilePath));
	ui->actionHTML_RTF->setChecked(true);
}

void CTextViewerWindow::find()
{
	ui->textBrowser->moveCursor(_findDialog.searchBackwards() ? QTextCursor::End : QTextCursor::Start);
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

	bool found = false;
	const QTextCursor startCursor = ui->textBrowser->textCursor();
	if (_findDialog.regex())
		found = ui->textBrowser->find(QRegExp(_findDialog.searchExpression(), _findDialog.caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive), flags);
	else
		found = ui->textBrowser->find(_findDialog.searchExpression(), flags);

	if(!found && (startCursor.isNull() || startCursor.position() == 0))
		QMessageBox::information(this, "Not found", QString("Expression \"")+expression+"\" not found");
	else if (!found && startCursor.position() > 0)
	{
		if (QMessageBox::question(this, "Not found", _findDialog.searchBackwards() ? "Beginning of file reached, do you want to restart search from the end?" : "End of file reached, do you want to restart search from the top?") == QMessageBox::Yes)
			find();
	}
}

QByteArray CTextViewerWindow::readSource() const
{
	QFile file(_sourceFilePath);
	if (file.exists() && file.open(QIODevice::ReadOnly))
		return file.readAll();

	return QByteArray();
}
