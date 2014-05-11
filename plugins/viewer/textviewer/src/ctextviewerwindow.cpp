#ifdef _WIN32
#pragma warning(push, 0) // set W0
#endif

#include "ctextviewerwindow.h"
#include "ui_ctextviewerwindow.h"

#ifdef _WIN32
#pragma warning(pop) // set W0
#endif

CTextViewerWindow::CTextViewerWindow(QWidget *parent) :
	CPluginWindow(parent),
	ui(new Ui::CTextViewerWindow)
{
	ui->setupUi(this);

	connect(ui->actionASCI, SIGNAL(triggered()), SLOT(asAscii()));
	connect(ui->actionUTF_8, SIGNAL(triggered()), SLOT(asUtf8()));
	connect(ui->actionUTF_16, SIGNAL(triggered()), SLOT(asUtf16()));
	connect(ui->actionHTML_RTF, SIGNAL(triggered()), SLOT(asRichText()));

	QActionGroup * group = new QActionGroup(this);
	group->addAction(ui->actionASCI);
	group->addAction(ui->actionUTF_8);
	group->addAction(ui->actionUTF_16);
	group->addAction(ui->actionHTML_RTF);
}

CTextViewerWindow::~CTextViewerWindow()
{
	delete ui;
}

bool CTextViewerWindow::loadTextFile(const QString& file)
{
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

QByteArray CTextViewerWindow::readSource() const
{
	QFile file(_sourceFilePath);
	if (file.exists() && file.open(QIODevice::ReadOnly))
		return file.readAll();

	return QByteArray();
}

