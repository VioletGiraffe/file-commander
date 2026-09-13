#include "ctextviewerwindow.h"

#include "cfinddialog.h"
#include "ctexteditwithimagesupport.h"


// Submodule includes
#include "qtcore_helpers/qt_helpers.hpp"
#include "widgets/clightningfastviewer.h"
#include "widgets/cpersistenceenabler.h"
#include "widgets/cplaintexteditwithlinenumbers.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/diegoiast/qutepart-cpp/hl/syntax_highlighter.h>
#include <3rdparty/diegoiast/qutepart-cpp/hl_factory.h>
#include <3rdparty/diegoiast/qutepart-cpp/theme.h>

#include <QAbstractScrollArea>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QStatusBar>
#include <QStringBuilder>
#include <QStyleHints>
#include <QTextCodec>
#include <QTextCursor>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>

// A NUL byte alone does not mean binary: decode() reads BOM-less UTF-16 and UTF-32 out of NUL-carrying input and
// declines the rest, running detection on neither
[[nodiscard]] static inline bool isBinaryContent(const QByteArray& data)
{
	return isBinary(data) && CTextEncodingDetector::decode(data).text.isEmpty();
}

static inline bool isNonAscii(char16_t c)
{
	if (c >= 32 && c <= 126)
		return false;
	return c != '\n' && c != '\r' && c != '\t';
}

static inline qsizetype countNonAsciiChars(const QString& text)
{
	return (qsizetype)std::count_if(text.begin(), text.end(), [](QChar c) {
		const auto code = c.unicode();
		return isNonAscii(code);
	});
}

CTextViewerWindow::CTextViewerWindow(QWidget* parent) noexcept :
	CPluginWindow(parent)
{
	setWindowTitle(tr("Text viewer"));
	setWindowIcon(QIcon{ QStringLiteral(":/main_icon") });

	enablePersistence(this, QStringLiteral("Plugins/TextViewer/Window"), CPersistenceEnabler::Delayed{ false });

	const auto addMenuAction = [this](QMenu* menu, const QString& text, const QString& shortcut, auto onTriggered) {
		QAction* const action = menu->addAction(text);
		action->setShortcut(QKeySequence{ shortcut });
		CR() = connect(action, &QAction::triggered, this, onTriggered);
		return action;
	};

	QMenu* const fileMenu = menuBar()->addMenu(tr("&File"));
	addMenuAction(fileMenu, tr("Open..."), "Ctrl+O", [this] {
		const QString fileName = QFileDialog::getOpenFileName(this);
		if (!fileName.isEmpty())
			loadTextFile(fileName);
	});
	addMenuAction(fileMenu, tr("Reload"), "F5", [this] { loadTextFile(_sourceFilePath); });
	fileMenu->addSeparator();
	addMenuAction(fileMenu, tr("Close"), {}, &QWidget::close);

	QMenu* const editMenu = menuBar()->addMenu(tr("&Edit"));
	addMenuAction(editMenu, tr("Find..."), "Ctrl+F", [this] {
		setupFindDialog();
		_findDialog->exec();
	});
	addMenuAction(editMenu, tr("Find next"), "F3", &CTextViewerWindow::findNext);

	QMenu* const viewMenu = menuBar()->addMenu(tr("&View"));
	addMenuAction(viewMenu, tr("Auto &detect encoding"), "1", [this] { redecodeCurrentFile(&CTextViewerWindow::asDetectedAutomatically); });

	auto* const viewAsGroup = new QActionGroup{ this };
	const auto addViewAsAction = [&](const QString& text, const QString& shortcut, auto onTriggered) {
		QAction* const action = addMenuAction(viewMenu, text, shortcut, onTriggered);
		action->setCheckable(true);
		viewAsGroup->addAction(action);
		return action;
	};

	_asciiAction = addViewAsAction(tr("&ASCII"), "2", [this] { redecodeCurrentFile(&CTextViewerWindow::asAscii); });
	_systemLocaleAction = addViewAsAction(tr("&System locale"), "3", [this] { redecodeCurrentFile(&CTextViewerWindow::asSystemDefault); });
	_utf8Action = addViewAsAction(tr("UTF-&8"), "4", [this] { redecodeCurrentFile(&CTextViewerWindow::asUtf8); });
	_utf16Action = addViewAsAction(tr("UTF-1&6"), "5", [this] { redecodeCurrentFile(&CTextViewerWindow::asUtf16); });
	_hexAction = addViewAsAction(tr("&Hex"), "6", [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asHexFast(*textData);
	});
	_htmlAction = addViewAsAction(tr("H&TML"), "7", [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asHtml(*textData);
	});
	_markdownAction = addViewAsAction(tr("&Markdown"), "8", [this] {
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (textData)
			asMarkdown(*textData);
	});

	viewMenu->addSeparator();
	_lineWrapAction = addMenuAction(viewMenu, tr("&Line wrap"), "Shift+W", &CTextViewerWindow::setLineWrap);
	_lineWrapAction->setCheckable(true);
	_lineWrapAction->setChecked(true); // Wrap by default

	new QShortcut{ QKeySequence{ Qt::Key_Escape }, this, this, [this] { close(); } };

	_encodingLabel = new QLabel(this);
	_contentTypeLabel = new QLabel(this);
	_infoLabel = new QLabel(this);
	_encodingLabel->setContentsMargins(4, 0, 4, 0);
	_contentTypeLabel->setContentsMargins(4, 0, 4, 0);
	_infoLabel->setContentsMargins(4, 0, 4, 0);

	auto* status = statusBar();
	status->addWidget(_encodingLabel);
	status->addWidget(_contentTypeLabel);
	status->addWidget(_infoLabel);

	_infoLabel->setVisible(false);
}

CTextViewerWindow::~CTextViewerWindow() = default;

// The syntax highlighter is what this limits: past it the Lightning viewer shows plain text instead
constexpr qsizetype maxSizeForSyntaxHighlighting = 1'000'000;
// detect() samples 256 KB whatever the input size, but the winning codec still decodes the whole file
constexpr qsizetype maxSizeForEncodingDetection = 25'000'000;

bool CTextViewerWindow::loadTextFile(const QString& file)
{
	QFileInfo fi(file);
	setWindowTitle(fi.fileName() + " [" + fi.path() + "]");
	// Only has effect on macOS , must be after set. Gives the title bar a document proxy icon (the only icon this window can have)
	setWindowFilePath(file);

	_sourceFilePath = file;

	_mimeType = QMimeDatabase().mimeTypeForFile(_sourceFilePath, QMimeDatabase::MatchContent).name();

	try
	{
		const std::optional<QByteArray> textData = readFileAndReportErrors();
		if (!textData)
			return false;

		const qsizetype dataSize = textData->size();

		// Binary content, and anything too large to decode, go to the fast viewer as Latin-1: every byte maps to a
		// character. Size first: isBinaryContent() decodes, so it must not see a file this branch already rejects.
		if (dataSize > maxSizeForEncodingDetection || isBinaryContent(*textData))
			return asAscii(*textData, true);

		const bool useFastMode = dataSize > maxSizeForSyntaxHighlighting;

		if (!useFastMode && (_sourceFilePath.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive) || _sourceFilePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)))
			return asHtml(*textData);
		else if (_sourceFilePath.endsWith(".md", Qt::CaseInsensitive) && !useFastMode)
			return asMarkdown(*textData);
		else if (_mimeType.contains("text") || _mimeType.isEmpty() || _mimeType.contains("octet-stream"))
			return asDetectedAutomatically(*textData, useFastMode);
		else if (asUtf8(*textData, useFastMode))
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
			setMode(Mode::Source);
			setTextAndApplyHighlighter(result->text);
		}

		encodingChanged(result->encoding, result->language);
		// Guess which matching encoding could be marked as selected in the menu
		if (result->encoding.compare("utf-8", Qt::CaseInsensitive) == 0)
			_utf8Action->setChecked(true);
		else if (result->encoding.startsWith(QStringLiteral("UTF-16"), Qt::CaseInsensitive))
			_utf16Action->setChecked(true);
		else if (result->encoding.contains("1251") || result->encoding.contains("1252"))
			_asciiAction->setChecked(true);
		else if (const auto systemCodecName = QTextCodec::codecForLocale()->name(); result->encoding.compare(systemCodecName, Qt::CaseInsensitive) == 0)
			_systemLocaleAction->setChecked(true);
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
		setMode(Mode::Source);
		setTextAndApplyHighlighter(text);
	}

	encodingChanged(codec->name());
	_systemLocaleAction->setChecked(true);

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
		setMode(Mode::Source);
		setTextAndApplyHighlighter(text);
	}

	encodingChanged("ASCII");
	_asciiAction->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf8(const QByteArray& fileData, bool useFastMode)
{
	// A NUL byte is valid UTF-8, so the decoder accepts binary: decode() guards this the same way before probing
	if (isBinary(fileData))
		return false;

	QString text;
	if (const CTextEncodingDetector::DecodedText decodedText = CTextEncodingDetector::decodeUtfBom(fileData); !decodedText.encoding.isEmpty())
	{
		if (decodedText.encoding.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) != 0)
			return false;

		text = decodedText.text;
	}
	else
	{
		auto utf8Text = decodeUtf8(fileData);
		if (!utf8Text)
			return false;

		text = std::move(*utf8Text);
	}

	if (useFastMode)
	{
		setMode(Mode::Lightning);
		_lightningViewer->setText(text);
	}
	else
	{
		setMode(Mode::Source);
		setTextAndApplyHighlighter(text);
	}

	encodingChanged("UTF-8");
	_utf8Action->setChecked(true);

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
		setMode(Mode::Source);
		setTextAndApplyHighlighter(text);
	}

	_utf16Action->setChecked(true);

	return true;
}

bool CTextViewerWindow::asHtml(const QByteArray& fileData)
{
	const auto result = decodeText(fileData);
	if (!result || (result->text.isEmpty() && !fileData.isEmpty()))
		return false;

	setMode(Mode::Rich);
	_richView->setHtml(result->text);
	_htmlAction->setChecked(true);
	return true;
}

bool CTextViewerWindow::asMarkdown(const QByteArray& fileData)
{
	const auto result = decodeUnicodeText(fileData);
	if (!result)
		return false;

	encodingChanged(result->encoding, result->language);
	setMode(Mode::Rich);
	_richView->setMarkdown(result->text);
	_markdownAction->setChecked(true);
	return true;
}

bool CTextViewerWindow::asHexFast(const QByteArray& fileData)
{
	setMode(Mode::Lightning);
	_lightningViewer->setData(fileData);
	encodingChanged(tr("none - viewing raw data"));
	_hexAction->setChecked(true);
	return true;
}

void CTextViewerWindow::redecodeCurrentFile(bool (CTextViewerWindow::*decoder)(const QByteArray&, bool))
{
	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
		return;

	// Null when the initial load failed: _sourceFilePath is set before the file is read, so an action can arrive with no viewer built
	QAbstractScrollArea* const scrollArea = viewer().widget;
	const int scrollPosition = scrollArea ? scrollArea->verticalScrollBar()->value() : 0;

	const Mode modeBefore = _currentMode;
	(this->*decoder)(*textData, _currentMode == Mode::Lightning);

	// A decoder can move Rich to Source, which destroys scrollArea; and each viewer scrolls in units of its own
	if (scrollArea && _currentMode == modeBefore)
		scrollArea->verticalScrollBar()->setValue(scrollPosition);
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
	auto result = CTextEncodingDetector::decode(textData);
	if (!result.encoding.isEmpty() || !result.text.isEmpty())
		return result;

	QTextCodec *codec = QTextCodec::codecForLocale();
	if (!codec)
		return {};

	result.encoding = codec->name();
	result.language = {};
	result.text = codec->toUnicode(textData);
	return result;
}

std::optional<CTextEncodingDetector::DecodedText> CTextViewerWindow::decodeUnicodeText(const QByteArray& textData)
{
	if (const CTextEncodingDetector::DecodedText decodedText = CTextEncodingDetector::decodeUtfBom(textData); !decodedText.encoding.isEmpty())
		return decodedText;

	return CTextEncodingDetector::DecodedText{QString::fromUtf8(textData), "UTF-8", {}, 0.0};
}

void CTextViewerWindow::find()
{
	setupFindDialog();

	const ViewerOps v = viewer();
	if (!v.widget)
		return;

	if (_findDialog->searchBackwards())
		v.moveToEnd();
	else
		v.moveToStart();

	findNext();
}

void CTextViewerWindow::findNext()
{
	setupFindDialog();

	const QString expression = _findDialog->searchExpression();
	if (expression.isEmpty())
		return;

	const ViewerOps v = viewer();
	if (!v.widget)
		return;

	QTextDocument::FindFlags flags {};
	if (_findDialog->caseSensitive())
		flags |= QTextDocument::FindCaseSensitively;
	if (_findDialog->searchBackwards())
		flags |= QTextDocument::FindBackward;
	if (_findDialog->wholeWords())
		flags |= QTextDocument::FindWholeWords;

	const qsizetype initialPosition = v.cursorPosition();

	const bool found = _findDialog->regex() ? v.findRegex(QRegularExpression{ expression }, flags) : v.findText(expression, flags);

	if (!found && (initialPosition == -1 || initialPosition == 0))
		QMessageBox::information(this, tr("Not found"), tr("Expression \"%1\" not found").arg(expression));
	else if (!found && initialPosition > 0)
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
	if (const ViewerOps v = viewer(); v.widget)
		v.setWordWrap(wrap);
}

void CTextViewerWindow::setupFindDialog()
{
	if (_findDialog)
		return;

	_findDialog = new CFindDialog(this, QStringLiteral("Plugins/TextViewer/Find/"));
	CR() = connect(_findDialog, &CFindDialog::find, this, &CTextViewerWindow::find);
	CR() = connect(_findDialog, &CFindDialog::findNext, this, &CTextViewerWindow::findNext);
}

CTextViewerWindow::ViewerOps CTextViewerWindow::viewer() const
{
	// One binding serves both document views: same calls, no base class that declares them
	const auto documentViewOps = [](auto* view) -> ViewerOps {
		return {
			view,
			[view](const QString& expression, QTextDocument::FindFlags flags) { return view->find(expression, flags); },
			[view](const QRegularExpression& expression, QTextDocument::FindFlags flags) { return view->find(expression, flags); },
			[view] { view->moveCursor(QTextCursor::Start); },
			[view] { view->moveCursor(QTextCursor::End); },
			[view] { return view->textCursor().isNull() ? qsizetype{ -1 } : (qsizetype)view->textCursor().position(); },
			[view](bool wrap) { view->setWordWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap); }
		};
	};

	if (_sourceView)
		return documentViewOps(_sourceView.get());

	if (_richView)
		return documentViewOps(_richView.get());

	if (auto* const view = _lightningViewer.get())
	{
		return {
			view,
			[view](const QString& expression, QTextDocument::FindFlags flags) { return view->find(expression, flags); },
			[view](const QRegularExpression& expression, QTextDocument::FindFlags flags) { return view->find(expression, flags); },
			[view] { view->moveToStart(); },
			[view] { view->moveToEnd(); },
			[view] { return view->selectionStart(); },
			[view](bool wrap) { view->setWordWrap(wrap); }
		};
	}

	return {};
}

// Scales the widget's own font so a line of it stands as tall as a line of the UI font. The family is the widget's to choose.
static void setFontSize(QWidget& w)
{
	QFont font = w.font();
	const int appFontH = QFontMetrics{ qApp->font() }.boundingRect(QChar{ 'M' }).height();
	const int newFontH = QFontMetrics{ font }        .boundingRect(QChar{ 'M' }).height();
	const qreal sizeRatio = newFontH > 0 ? ((qreal)appFontH / (qreal)newFontH) : 1.0;
	font.setPointSizeF(font.pointSizeF() * sizeRatio);
	w.setFont(font);
}

// A template because the source and rich views answer these calls without sharing a base that declares them
template <typename DocumentView>
static void initDocumentView(DocumentView& view)
{
	// Both the tab stop distance below and the wrap width depend on the font, so it goes on first
	view.setFont(CLightningFastViewerWidget::preferredFixedFont());
	setFontSize(view);

	view.setReadOnly(true);
	view.setUndoRedoEnabled(false);
	view.setTabStopDistance(static_cast<qreal>(4 * view.fontMetrics().horizontalAdvance(' ')));
}

void CTextViewerWindow::setMode(Mode mode)
{
	_currentMode = mode;

	if (mode != Mode::Source)
	{
		resetHighlighter(); // Holds the source view's document, so it goes before that view is destroyed
		_sourceView.reset();
		updateContentTypeLabel(); // Names the highlighter's language while there is one, the MIME type otherwise
	}

	if (mode != Mode::Rich)
		_richView.reset();

	if (mode != Mode::Lightning)
		_lightningViewer.reset();

	switch (mode)
	{
	case Mode::Source:
		if (!_sourceView)
		{
			_sourceView = std::make_unique<CPlainTextEditWithLineNumbers>(this);
			initDocumentView(*_sourceView);
			setCentralWidget(_sourceView.get());
		}
		break;

	case Mode::Rich:
		if (!_richView)
		{
			_richView = std::make_unique<CTextEditWithImageSupport>(this);
			initDocumentView(*_richView);
			_richView->setAcceptRichText(true);
			setCentralWidget(_richView.get());
		}
		break;

	case Mode::Lightning:
		if (!_lightningViewer)
		{
			_lightningViewer = std::make_unique<CLightningFastViewerWidget>(this);
			setFontSize(*_lightningViewer);
			setCentralWidget(_lightningViewer.get());
		}

		_infoLabel->setText(tr("FAST MODE! Encoding detection didn't run, you can trigger it manually"));
		break;
	}

	_infoLabel->setVisible(mode == Mode::Lightning);

	// Apply line wrap setting
	setLineWrap(_lineWrapAction->isChecked());
}

void CTextViewerWindow::setTextAndApplyHighlighter(const QString& text)
{
	if (const auto size = text.size(); size < 1'000'000 && countNonAsciiChars(text) < size / 10)
	{
		const QString langId = Qutepart::chooseLanguageXmlFileName(_mimeType, QString(), _sourceFilePath, text.left(100));
		qInfo() << "Language detected:" << langId;

		resetHighlighter();
		_highlighter = static_cast<Qutepart::SyntaxHighlighter*>(Qutepart::makeHighlighter(_sourceView->document(), langId));
		if (_highlighter)
		{
			_theme = std::make_unique<Qutepart::Theme>();
			QStyleHints* styleHints = QApplication::styleHints();
			_theme->loadTheme(styleHints && styleHints->colorScheme() == Qt::ColorScheme::Dark ? ":/qutepart/themes/monokai.theme" : ":/qutepart/themes/homunculus.theme");
			_highlighter->setTheme(_theme.get());
		}
	}

	updateContentTypeLabel();
	_sourceView->setPlainText(text);
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

void CTextViewerWindow::updateContentTypeLabel()
{
	QString format = _highlighter ? _highlighter->languageName() : QString{};
	if (format.isEmpty())
		format = _mimeType;

	_contentTypeLabel->setText(tr("Content format: ") + format);
}
