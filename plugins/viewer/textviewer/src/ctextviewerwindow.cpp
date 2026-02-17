#include "ctextviewerwindow.h"
#include "ctexteditwithimagesupport.h"
#include "cfinddialog.h"
#include "clightningfastviewer.h"

#include "widgets/cpersistentwindow.h"
#include "qtcore_helpers/qt_helpers.hpp"

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

inline bool isNonAscii(char16_t c)
{
	if (c >= 32 && c <= 126)
		return false;
	return c != '\n' && c != '\r' && c != '\t';
}

inline qsizetype countNonAsciiChars(const QByteArray& data)
{
	return (qsizetype)std::count_if(data.begin(), data.end(), &isNonAscii);
}

inline qsizetype countNonAsciiChars(const QString& text)
{
	return (qsizetype)std::count_if(text.begin(), text.end(), [](QChar c) {
		const auto code = c.unicode();
		return isNonAscii(code);
	});
}

CTextViewerWindow::CTextViewerWindow(QWidget* parent) noexcept :
	CPluginWindow(parent)
{
	setupUi(this);

	installEventFilter(new CPersistenceEnabler(QStringLiteral("Plugins/TextViewer/Window"), this, false));

	CR() = connect(actionOpen, &QAction::triggered, this, [this]() {
		const QString fileName = QFileDialog::getOpenFileName(this);
		if (!fileName.isEmpty())
			loadTextFile(fileName);
	});

	CR() = connect(actionReload, &QAction::triggered, this, [this]() {
		loadTextFile(_sourceFilePath);
	});
	CR() = connect(actionClose, &QAction::triggered, this, &QDialog::close);

	CR() = connect(actionFind, &QAction::triggered, this, [this]() {
		if (!_textView)
			return;
		setupFindDialog();
		_findDialog->exec();
	});
	CR() = connect(actionFind_next, &QAction::triggered, this, &CTextViewerWindow::findNext);

	CR() = connect(actionAuto_detect_encoding, &QAction::triggered, this, [this]{
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asDetectedAutomatically(*textData, _currentMode == Mode::Lightning);
	});
	CR() = connect(actionASCII_Windows_1252, &QAction::triggered, this, [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asAscii(*textData, _currentMode == Mode::Lightning);
	});
	CR() = connect(actionSystemLocale, &QAction::triggered, this, [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asSystemDefault(*textData, _currentMode == Mode::Lightning);
	});
	connect(actionUTF_8, &QAction::triggered, this, [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asUtf8(*textData, _currentMode == Mode::Lightning);
	});
	CR() = connect(actionUTF_16, &QAction::triggered, this, [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asUtf16(*textData, _currentMode == Mode::Lightning);
	});
	CR() = connect(actionHTML, &QAction::triggered, this, [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asHtml(*textData);
	});
	CR() = connect(actionMarkdown, &QAction::triggered, this, [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asMarkdown(*textData);
	});
	CR() = connect(actionHex, &QAction::triggered, this, [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asHexFast(*textData);
	});

	QActionGroup * group = new QActionGroup(this);
	group->setExclusive(true);
	group->addAction(actionASCII_Windows_1252);
	group->addAction(actionSystemLocale);
	group->addAction(actionUTF_8);
	group->addAction(actionUTF_16);
	group->addAction(actionHTML);
	group->addAction(actionMarkdown);
	group->addAction(actionHex);

	CR() = connect(actionLine_wrap, &QAction::triggered, this, &CTextViewerWindow::setLineWrap);
	actionLine_wrap->setChecked(true); // Wrap by default

	auto* escScut = new QShortcut(QKeySequence("Esc"), this, SLOT(close()));
	CR() = connect(this, &QObject::destroyed, escScut, &QShortcut::deleteLater);

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
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (!textData)
			return false;

		const auto dataSize = textData->size();
		const bool useFastMode = dataSize > 1'000'000 || countNonAsciiChars(*textData) >= dataSize / 8;
		if (useFastMode)
			return asAscii(*textData, useFastMode);

		if (_sourceFilePath.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive) || _sourceFilePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive))
			return asHtml(*textData);
		else if (_sourceFilePath.endsWith(".md", Qt::CaseInsensitive) && !useFastMode)
			return asMarkdown(*textData);
		else if (!useFastMode && (_mimeType.contains(QStringLiteral("text")) || _mimeType.isEmpty()))
			return asDetectedAutomatically(*textData, useFastMode);

		if (asUtf8(*textData, useFastMode))
			return true;
		else
			return asAscii(*textData, useFastMode);
	}
	catch (const std::bad_alloc&)
	{
		QMessageBox::warning(this, tr("File is too large"), tr("The file is too large to handle."));
		return false;
	}
}

bool CTextViewerWindow::asDetectedAutomatically(const QByteArray& fileData, bool useFastMode)
{
	const auto result = decodeText(fileData);
	if (result && !result->text.isEmpty())
	{
		if (useFastMode)
		{
			// Use the fast plain text mode
			setMode(Mode::Lightning);
			_lightningViewer->setText(result->text);
		}
		else
		{
			setMode(Mode::Full);
			setTextAndApplyHighlighter(result->text);
		}

		encodingChanged(result->encoding, result->language);
		// Guess which matching encoding could be marked as selected in the menu
		if (result->encoding.compare("utf-8", Qt::CaseInsensitive) == 0)
			actionUTF_8->setChecked(true);
		else if (result->encoding.compare("utf-16", Qt::CaseInsensitive) == 0)
			actionUTF_16->setChecked(true);
		else if (result->encoding.contains("1251") || result->encoding.contains("1252"))
			actionASCII_Windows_1252->setChecked(true);
		else if (const auto systemCodecName = QTextCodec::codecForLocale()->name(); result->encoding.compare(systemCodecName, Qt::CaseInsensitive) == 0)
			actionSystemLocale->setChecked(true);
		return true;
	}

	if (asSystemDefault(fileData, useFastMode))
		return true;
	else
		return asAscii(fileData, useFastMode);
}

bool CTextViewerWindow::asSystemDefault(const QByteArray& fileData, bool useFastMode)
{
	QTextCodec * codec = QTextCodec::codecForLocale();
	if (!codec)
		return false;

	const QString text = codec->toUnicode(fileData);
	if (useFastMode)
	{
		setMode(Mode::Lightning);
		_lightningViewer->setText(text);
	}
	else
	{
		setMode(Mode::Full);
		setTextAndApplyHighlighter(text);
	}

	encodingChanged(codec->name());
	actionSystemLocale->setChecked(true);

	return true;
}

bool CTextViewerWindow::asAscii(const QByteArray& fileData, bool useFastMode)
{
	const QString text = QString::fromLatin1(fileData);
	if (useFastMode)
	{
		setMode(Mode::Lightning);
		_lightningViewer->setText(text);
	}
	else
	{
		setMode(Mode::Full);
		setTextAndApplyHighlighter(text);
	}

	encodingChanged("ASCII");
	actionASCII_Windows_1252->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf8(const QByteArray& fileData, bool useFastMode)
{
	const QString text = QString::fromUtf8(fileData);
	if (useFastMode)
	{
		setMode(Mode::Lightning);
		_lightningViewer->setText(text);
	}
	else
	{
		setMode(Mode::Full);
		setTextAndApplyHighlighter(text);
	}

	encodingChanged("UTF-8");
	actionUTF_8->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf16(const QByteArray& fileData, bool useFastMode)
{
	encodingChanged("UTF-16");
	static_assert (std::is_trivially_copyable_v<QChar>);
	
	const QString text = QString::fromUtf16(reinterpret_cast<const char16_t*>(fileData.constData()), fileData.size() / 2);
	if (useFastMode)
	{
		setMode(Mode::Lightning);
		_lightningViewer->setText(text);
	}
	else
	{
		setMode(Mode::Full);
		setTextAndApplyHighlighter(text);
	}

	actionUTF_16->setChecked(true);

	return true;
}

bool CTextViewerWindow::asHtml(const QByteArray& fileData)
{
	const auto result = decodeText(fileData);
	if (!result || result->text.isEmpty())
		return false;

	resetHighlighter();
	setMode(Mode::Full);
	_textView->setHtml(result->text);
	actionHTML->setChecked(true);
	return true;
}

bool CTextViewerWindow::asMarkdown(const QByteArray& fileData)
{
	const auto result = decodeText(fileData);
	if (!result || result->text.isEmpty())
		return false;

	resetHighlighter();
	encodingChanged(result->encoding, result->language);
	setMode(Mode::Full);
	_textView->setMarkdown(result->text);
	actionMarkdown->setChecked(true);
	return true;
}

bool CTextViewerWindow::asHexFast(const QByteArray& fileData)
{
	setMode(Mode::Lightning);
	_lightningViewer->setData(fileData);
	encodingChanged(tr("none - viewing raw data"));
	actionHex->setChecked(true);
	return true;
}

std::optional<QByteArray> CTextViewerWindow::readFileAndReportErrors() const
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

	_textView->moveCursor(_findDialog->searchBackwards() ? QTextCursor::End : QTextCursor::Start);
	findNext();
}

void CTextViewerWindow::findNext()
{
	if (!_textView)
		return;

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

	const QTextCursor startCursor = _textView->textCursor();
	bool found = false;
	if (_findDialog->regex())
		found = _textView->find(QRegularExpression(_findDialog->searchExpression()), flags);
	else
		found = _textView->find(_findDialog->searchExpression(), flags);

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
	if (_textView)
		_textView->setWordWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
	if (_lightningViewer)
		_lightningViewer->setWordWrap(wrap);
}

void CTextViewerWindow::setupFindDialog()
{
	if (_findDialog)
		return;

	_findDialog = new CFindDialog(this, QStringLiteral("Plugins/TextViewer/Find/"));
	CR() = connect(_findDialog, &CFindDialog::find, this, &CTextViewerWindow::find);
	CR() = connect(_findDialog, &CFindDialog::findNext, this, &CTextViewerWindow::findNext);
}

void CTextViewerWindow::setMode(Mode mode)
{
	static const auto setFixedFont = [](QWidget& w) {
		QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		QFontMetrics fm{ fixedFont };
		const qreal sizeReatio = fm.height() > 0 ? ((qreal)QFontMetrics { qApp->font() }.height() / (qreal)fm.height()) : 1.0;
		fixedFont.setPointSizeF(fixedFont.pointSizeF() * sizeReatio);
		w.setFont(fixedFont);
	};

	_currentMode = mode;
	if (mode == Mode::Full)
	{
		_lightningViewer.reset();
		if (!_textView)
		{
			_textView = std::make_unique<CTextEditWithImageSupport>(this);
			_textView->setReadOnly(true);
			_textView->setUndoRedoEnabled(false);
			_textView->setTabStopDistance(static_cast<qreal>(4 * _textView->fontMetrics().horizontalAdvance(' ')));
			_textView->setAcceptRichText(true);

			setFixedFont(*_textView);
			setCentralWidget(_textView.get());
		}
	}
	else
	{
		resetHighlighter();
		_textView.reset();
		if (!_lightningViewer)
		{
			_lightningViewer = std::make_unique<CLightningFastViewerWidget>(this);
			setFixedFont(*_lightningViewer);
			setCentralWidget(_lightningViewer.get());
		}
	}

	// Apply line wrap setting
	setLineWrap(actionLine_wrap->isChecked());
}

void CTextViewerWindow::setTextAndApplyHighlighter(const QString& text)
{
	if (const auto size = text.size(); size < 1'000'000 && countNonAsciiChars(text) < size / 10)
	{
		const QString langId = Qutepart::chooseLanguageXmlFileName(_mimeType, QString(), _sourceFilePath, text.left(100));
		qInfo() << "Language detected:" << langId;

		resetHighlighter();
		_highlighter = static_cast<Qutepart::SyntaxHighlighter*>(Qutepart::makeHighlighter(_textView->document(), langId));
		if (_highlighter)
		{
			_theme = std::make_unique<Qutepart::Theme>();
			QStyleHints* styleHints = QApplication::styleHints();
			_theme->loadTheme(styleHints && styleHints->colorScheme() == Qt::ColorScheme::Dark ? ":/qutepart/themes/monokai.theme" : ":/qutepart/themes/homunculus.theme");
			_highlighter->setTheme(_theme.get());
		}
	}

	_textView->setPlainText(text);
}

void CTextViewerWindow::resetHighlighter()
{
	if (!_highlighter)
		return;

	_highlighter->setDocument(nullptr);
	delete _highlighter;
	_highlighter = nullptr;
	_theme.reset();
}
