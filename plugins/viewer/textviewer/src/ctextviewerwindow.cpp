#include "ctextviewerwindow.h"
#include "ctexteditwithimagesupport.h"
#include "cfinddialog.h"

#include "widgets/cpersistentwindow.h"

#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include "3rdparty/diegoiast/qutepart-cpp/hl_factory.h"
#include "3rdparty/diegoiast/qutepart-cpp/hl/syntax_highlighter.h"
#include "3rdparty/diegoiast/qutepart-cpp/theme.h"

#include <QApplication>
#include <QActionGroup>
#include <QDebug>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLabel>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QShortcut>
#include <QStringBuilder>
#include <QStyleHints>
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <type_traits>

CTextViewerWindow::CTextViewerWindow(QWidget* parent) noexcept :
	CPluginWindow(parent)
{
	setupUi(this);

	_textBrowser = new CTextEditWithImageSupport(this);

	{
		QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		QFontMetrics fm{fixedFont};
		const qreal sizeReatio = fm.height() > 0 ? ((qreal)QFontMetrics{qApp->font()}.height() / (qreal)fm.height()) : 1.0;
		fixedFont.setPointSizeF(fixedFont.pointSizeF() * sizeReatio);
		_textBrowser->setFont(fixedFont);
	}

	installEventFilter(new CPersistenceEnabler(QStringLiteral("Plugins/TextViewer/Window"), this));

	setCentralWidget(_textBrowser);
	_textBrowser->setReadOnly(true);
	_textBrowser->setUndoRedoEnabled(false);
	_textBrowser->setTabStopDistance(static_cast<qreal>(4 * _textBrowser->fontMetrics().horizontalAdvance(' ')));
	_textBrowser->setAcceptRichText(true);

	assert_r(connect(actionOpen, &QAction::triggered, this, [this]() {
		const QString fileName = QFileDialog::getOpenFileName(this);
		if (!fileName.isEmpty())
			loadTextFile(fileName);
	}));

	assert_r(connect(actionReload, &QAction::triggered, this, [this]() {
		loadTextFile(_sourceFilePath);
	}));
	assert_r(connect(actionClose, &QAction::triggered, this, &QDialog::close));

	assert_r(connect(actionFind, &QAction::triggered, this, [this]() {
		setupFindDialog();
		_findDialog->exec();
	}));
	assert_r(connect(actionFind_next, &QAction::triggered, this, &CTextViewerWindow::findNext));

	assert_r(connect(actionAuto_detect_encoding, &QAction::triggered, this, &CTextViewerWindow::asDetectedAutomatically));
	assert_r(connect(actionASCII_Windows_1252, &QAction::triggered, this, &CTextViewerWindow::asAscii));
	assert_r(connect(actionSystemLocale, &QAction::triggered, this, &CTextViewerWindow::asSystemDefault));
	assert_r(connect(actionUTF_8, &QAction::triggered, this, &CTextViewerWindow::asUtf8));
	assert_r(connect(actionUTF_16, &QAction::triggered, this, &CTextViewerWindow::asUtf16));
	assert_r(connect(actionHTML, &QAction::triggered, this, &CTextViewerWindow::asHtml));
	assert_r(connect(actionMarkdown, &QAction::triggered, this, &CTextViewerWindow::asMarkdown));

	QActionGroup * group = new QActionGroup(this);
	group->addAction(actionASCII_Windows_1252);
	group->addAction(actionSystemLocale);
	group->addAction(actionUTF_8);
	group->addAction(actionUTF_16);
	group->addAction(actionHTML);
	group->addAction(actionMarkdown);

	assert_r(connect(actionLine_wrap, &QAction::triggered, this, &CTextViewerWindow::setLineWrap));
	actionLine_wrap->setChecked(true); // Wrap by default

	auto* escScut = new QShortcut(QKeySequence(QStringLiteral("Esc")), this, SLOT(close()));
	assert_r(connect(this, &QObject::destroyed, escScut, &QShortcut::deleteLater));

	_encodingLabel = new QLabel(this);
	QMainWindow::statusBar()->addWidget(_encodingLabel);
}

CTextViewerWindow::~CTextViewerWindow() = default;

bool CTextViewerWindow::loadTextFile(const QString& file)
{
	setWindowTitle(file);
	_sourceFilePath = file;

	_mimeType = QMimeDatabase().mimeTypeForFile(_sourceFilePath, QMimeDatabase::MatchContent).name();

	try
	{
		if (_sourceFilePath.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive) || _sourceFilePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive))
			return asHtml();
		else if (_sourceFilePath.endsWith(".md", Qt::CaseInsensitive))
			return asMarkdown();
		else if (_mimeType.contains(QStringLiteral("text")) || _mimeType.isEmpty())
			return asDetectedAutomatically();
		else
			return asAscii();
	}
	catch (const std::bad_alloc&)
	{
		QMessageBox::warning(this, tr("File is too large"), tr("The text is too large to display"));
		return false;
	}
}

bool CTextViewerWindow::asDetectedAutomatically()
{
	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
		return false;

	const auto result = decodeText(*textData);
	if (!result || result->text.isEmpty())
		return asSystemDefault();

	encodingChanged(result->encoding, result->language);
	setTextAndApplyHighlighter(result->text);
	return true;
}

bool CTextViewerWindow::asSystemDefault()
{
	QTextCodec * codec = QTextCodec::codecForLocale();
	if (!codec)
		return false;

	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
		return false;

	setTextAndApplyHighlighter(codec->toUnicode(*textData));
	encodingChanged(codec->name());
	actionSystemLocale->setChecked(true);

	return true;
}

bool CTextViewerWindow::asAscii()
{
	QTextCodec* codec = QTextCodec::codecForName("Windows-1252");
	if (!codec)
		return false;

	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
		return false;

	setTextAndApplyHighlighter(codec->toUnicode(*textData));
	encodingChanged(codec->name());
	actionASCII_Windows_1252->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf8()
{
	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
		return false;

	encodingChanged("UTF-8");
	setTextAndApplyHighlighter(QString::fromUtf8(*textData));
	actionUTF_8->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf16()
{
	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
		return false;

	encodingChanged("UTF-16");
	static_assert (std::is_trivially_copyable_v<QChar>);
	QString text;
	text.resize(textData->size() / 2 + 1, QChar{'\0'});
	::memcpy(text.data(), textData->constData(), static_cast<size_t>(textData->size()));
	setTextAndApplyHighlighter(text);
	actionUTF_16->setChecked(true);

	return true;
}

bool CTextViewerWindow::asHtml()
{
	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
		return false;

	const auto result = decodeText(*textData);
	if (!result || result->text.isEmpty())
		return false;

	_textBrowser->setHtml(result->text);
	return true;
}

bool CTextViewerWindow::asMarkdown()
{
	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
		return false;

	const auto result = decodeText(*textData);
	if (!result || result->text.isEmpty())
		return false;

	encodingChanged(result->encoding, result->language);
	_textBrowser->setMarkdown(result->text);
	return true;
}

std::optional<QByteArray> CTextViewerWindow::readFileAndReportErrors()
{
	QByteArray textData;
	if (!readSource(textData))
	{
		QMessageBox::warning(parentWidget(), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return {};
	}

	return std::optional{ std::move(textData) };
}

std::optional<CTextEncodingDetector::DecodedText> CTextViewerWindow::decodeText(const QByteArray& textData)
{
	QTextCodec::ConverterState state;
	QTextCodec *codec = QTextCodec::codecForName("UTF-8");
	if (!codec)
		return {};

	QString text = codec->toUnicode(textData.constData(), (int)textData.size(), &state);
	if (state.invalidChars > 0)
	{
		auto result = CTextEncodingDetector::decode(textData);
		if (!result.text.isEmpty())
			return result;

		codec = QTextCodec::codecForLocale();
		if (!codec)
			return {};

		result.encoding = codec->name();
		result.language = {};
		result.text = codec->toUnicode(textData);
		return std::move(result);
	}
	else
	{
		return CTextEncodingDetector::DecodedText{.text = std::move(text), .encoding = "utf-8", .language = {}};
	}
}

void CTextViewerWindow::find()
{
	setupFindDialog();

	_textBrowser->moveCursor(_findDialog->searchBackwards() ? QTextCursor::End : QTextCursor::Start);
	findNext();
}

void CTextViewerWindow::findNext()
{
	setupFindDialog();

	const QString expression = _findDialog->searchExpression();
	if (expression.isEmpty())
		return;

	QTextDocument::FindFlags flags {};
	if (_findDialog->caseSensitive())
		flags |= QTextDocument::FindCaseSensitively;
	if (_findDialog->searchBackwards())
		flags |= QTextDocument::FindBackward;
	if (_findDialog->wholeWords())
		flags |= QTextDocument::FindWholeWords;

	const QTextCursor startCursor = _textBrowser->textCursor();
	bool found = false;
	if (_findDialog->regex())
		found = _textBrowser->find(QRegularExpression(_findDialog->searchExpression()), flags);
	else
		found = _textBrowser->find(_findDialog->searchExpression(), flags);

	if(!found && (startCursor.isNull() || startCursor.position() == 0))
		QMessageBox::information(this, tr("Not found"), tr("Expression \"%1\" not found").arg(expression));
	else if (!found && startCursor.position() > 0)
	{
		if (QMessageBox::question(this, tr("Not found"), _findDialog->searchBackwards() ? tr("Beginning of file reached, do you want to restart search from the end?") : tr("End of file reached, do you want to restart search from the top?")) == QMessageBox::Yes)
			find();
	}
}

bool CTextViewerWindow::readSource(QByteArray& textData) const
{
	QFile file(_sourceFilePath);
	if (file.exists() && file.open(QIODevice::ReadOnly))
	{
		textData = file.readAll();
		return textData.size() > 0 || file.size() == 0;
	}
	else
		return false;
}

void CTextViewerWindow::encodingChanged(const QString& encoding, const QString& language)
{
	QString message;
	if (!encoding.isEmpty())
		message = tr("Text encoding: ") % encoding;
	if (!language.isEmpty())
		message = message % ", " % tr("language: ") % language;

	_encodingLabel->setText(message);
}

void CTextViewerWindow::setLineWrap(bool wrap)
{
	_textBrowser->setWordWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
}

void CTextViewerWindow::setupFindDialog()
{
	if (_findDialog)
		return;

	_findDialog = new CFindDialog(this, QStringLiteral("Plugins/TextViewer/Find/"));
	assert_r(connect(_findDialog, &CFindDialog::find, this, &CTextViewerWindow::find));
	assert_r(connect(_findDialog, &CFindDialog::findNext, this, &CTextViewerWindow::findNext));
}

void CTextViewerWindow::setTextAndApplyHighlighter(const QString& text)
{
	static const auto countNonAsciiChars = [](const QString& str) {
		return std::count_if(str.begin(), str.end(), [](QChar c) {
			const auto code = c.unicode();
			if (code >= 32 && code <= 126)
				return false;
			else if (code == '\n' || code == '\r' || code == '\t')
				return false;
			return true;
		});
	};

	if (const auto size = text.size(); size < 1'000'000 && countNonAsciiChars(text) < size / 10)
	{
		const QString langId = Qutepart::chooseLanguageXmlFileName(_mimeType, QString(), _sourceFilePath, text.left(100));
		qInfo() << "Language detected:" << langId;

		auto* highlighter = static_cast<Qutepart::SyntaxHighlighter*>(Qutepart::makeHighlighter(_textBrowser->document(), langId));
		if (highlighter)
		{
			_theme = std::make_unique<Qutepart::Theme>();
			QStyleHints* styleHints = QApplication::styleHints();
			_theme->loadTheme(styleHints && styleHints->colorScheme() == Qt::ColorScheme::Dark ? ":/qutepart/themes/monokai.theme" : ":/qutepart/themes/homunculus.theme");
			highlighter->setTheme(_theme.get());
		}

		_syntaxHighlighter.reset(highlighter);
	}
	else
	{
		_syntaxHighlighter.reset();
	}

	_textBrowser->setPlainText(text);
}
