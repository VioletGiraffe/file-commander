#include "ctextviewerwindow.h"


// Submodule includes
#include "qtcore_helpers/qt_helpers.hpp"
#include "theme/colorutils.h"
#include "widgets/cfindbar.h"
#include "widgets/clightningfastviewer.h"
#include "widgets/cpersistenceenabler.h"
#include "widgets/cplaintexteditwithlinenumbers.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/diegoiast/qutepart-cpp/include/qutepart/theme.h>
#include <3rdparty/diegoiast/qutepart-cpp/src/hl/syntax_highlighter.h>
#include <3rdparty/diegoiast/qutepart-cpp/src/hl_factory.h>

#include <QAbstractScrollArea>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QDeadlineTimer>
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
#include <QPalette>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QStatusBar>
#include <QStringBuilder>
#include <QStringDecoder>
#include <QStyleHints>
#include <QTextCharFormat>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <new>
#include <optional>
#include <string_view>
#include <utility>

// A NUL byte alone does not mean binary: decode() reads BOM-less UTF-16 and UTF-32 out of NUL-carrying input and
// declines the rest, running detection on neither
[[nodiscard]] static inline bool isBinaryContent(const QByteArray& data)
{
	return isBinary(data) && CTextEncodingDetector::decode(data).text.isEmpty();
}

CTextViewerWindow::CTextViewerWindow(QWidget* parent) noexcept :
	CPluginWindow(parent)
{
	setWindowTitle(tr("Text viewer"));
	setWindowIcon(QIcon{ QStringLiteral(":/main_icon") });

	enablePersistence(this, QStringLiteral("Plugins/TextViewer/Window"), CPersistenceEnabler::Delayed{ false });

	auto* const central = new QWidget{ this };
	_centralLayout = new QVBoxLayout{ central };
	_centralLayout->setContentsMargins(0, 0, 0, 0);
	_centralLayout->setSpacing(0);
	setCentralWidget(central);

	_findBar = new CFindBar{
		CFindBar::HostFunctions{
			.findText = [this](const QString& pattern, QTextDocument::FindFlags flags) { const ViewerOps v = viewer(); return v.widget ? v.findText(pattern, flags) : FindResult::NotFound; },
			.findRegex = [this](const QRegularExpression& pattern, QTextDocument::FindFlags flags) { const ViewerOps v = viewer(); return v.widget ? v.findRegex(pattern, flags) : FindResult::NotFound; },
			.countText = [this](const QString& pattern, QTextDocument::FindFlags flags, QDeadlineTimer deadline, bool highlight) {
				const ViewerOps v = viewer();
				return v.widget ? v.countText(pattern, flags, deadline, highlight) : MatchCount{};
			},
			.countRegex = [this](const QRegularExpression& pattern, QTextDocument::FindFlags flags, QDeadlineTimer deadline, bool highlight) {
				const ViewerOps v = viewer();
				return v.widget ? v.countRegex(pattern, flags, deadline, highlight) : MatchCount{};
			},
			.clearHighlights = [this] {
				if (const ViewerOps v = viewer(); v.widget)
					v.clearHighlights();
			},
		},
		CFindBar::Keys{ .find = QStringLiteral("Ctrl+F"), .findNext = QStringLiteral("F3"), .findPrevious = QStringLiteral("Shift+F3") },
		QStringLiteral("Plugins/TextViewer/Find")
	};
	_centralLayout->addWidget(_findBar);

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

	menuBar()->addMenu(tr("&Edit"))->addActions(_findBar->findActions());

	QMenu* const viewMenu = menuBar()->addMenu(tr("&View"));
	addMenuAction(viewMenu, tr("Auto &detect encoding"), "1", [this] { redecodeCurrentFile(&CTextViewerWindow::asDetectedAutomatically); });

	_viewAsGroup = new QActionGroup{ this };
	const auto addViewAsAction = [&](const QString& text, const QString& shortcut, auto onTriggered) {
		QAction* const action = addMenuAction(viewMenu, text, shortcut, onTriggered);
		action->setCheckable(true);
		_viewAsGroup->addAction(action);
		return action;
	};

	_asciiAction = addViewAsAction(tr("&ASCII"), "2", [this] { redecodeCurrentFile(&CTextViewerWindow::asAscii); });
	_systemLocaleAction = addViewAsAction(tr("&System locale"), "3", [this] { redecodeCurrentFile(&CTextViewerWindow::asSystemDefault); });
	_utf8Action = addViewAsAction(tr("UTF-&8"), "4", [this] { redecodeCurrentFile(&CTextViewerWindow::asUtf8); });
	_utf16Action = addViewAsAction(tr("UTF-1&6"), "5", [this] { redecodeCurrentFile(&CTextViewerWindow::asUtf16); });
	_hexAction = addViewAsAction(tr("&Hex"), "6", [this] { renderCurrentFile(&CTextViewerWindow::asHexFast); });
	_htmlAction = addViewAsAction(tr("H&TML"), "7", [this] { renderCurrentFile(&CTextViewerWindow::asHtml); });
	_markdownAction = addViewAsAction(tr("&Markdown"), "8", [this] { renderCurrentFile(&CTextViewerWindow::asMarkdown); });

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
constexpr qsizetype maxSizeForSyntaxHighlighting = 3'000'000; // 3 MB of .c code = 800 ms on Core i5-12500
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
	const auto result = CTextEncodingDetector::decodeWithLocaleFallback(fileData);
	if (!result.text.isEmpty())
	{
		if (useFastMode)
		{
			// Use the fast plain text mode
			setMode(Mode::Lightning);
			_lightningViewer->setText(result.text);
		}
		else
		{
			setMode(Mode::Source);
			setTextAndApplyHighlighter(result.text);
		}

		encodingChanged(result.encoding, result.language);
		// Guess which matching encoding could be marked as selected in the menu
		if (result.encoding.compare("utf-8", Qt::CaseInsensitive) == 0)
			setViewAsAction(_utf8Action);
		else if (result.encoding.startsWith(QStringLiteral("UTF-16"), Qt::CaseInsensitive))
			setViewAsAction(_utf16Action);
		else if (result.encoding.compare(QStringLiteral("ISO-8859-1"), Qt::CaseInsensitive) == 0)
			setViewAsAction(_asciiAction); // The ASCII action decodes as Latin-1
		else if (const auto systemCodecName = QTextCodec::codecForLocale()->name(); result.encoding.compare(systemCodecName, Qt::CaseInsensitive) == 0)
			setViewAsAction(_systemLocaleAction);
		else
			setViewAsAction(nullptr);
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
	setViewAsAction(_systemLocaleAction);

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
	setViewAsAction(_asciiAction);

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
	setViewAsAction(_utf8Action);

	return true;
}

bool CTextViewerWindow::asUtf16(const QByteArray& fileData, bool useFastMode)
{
	encodingChanged("UTF-16");

	// Byte order: a BOM first, then the NUL layout, as decode() detects it; the host's without either
	QStringDecoder::Encoding byteOrder = QStringDecoder::Utf16;
	const bool hasBom = fileData.startsWith("\xFF\xFE") || fileData.startsWith("\xFE\xFF");
	if (const char* const wideEncoding = hasBom ? nullptr : CTextEncodingDetector::wideEncodingFromNulLayout(fileData))
		byteOrder = std::string_view{ wideEncoding }.ends_with("BE") ? QStringDecoder::Utf16BE : QStringDecoder::Utf16LE;

	const QString text = QStringDecoder{ byteOrder }(fileData);
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

	setViewAsAction(_utf16Action);

	return true;
}

bool CTextViewerWindow::asHtml(const QByteArray& fileData)
{
	const auto result = CTextEncodingDetector::decodeWithLocaleFallback(fileData);
	if (result.text.isEmpty() && !fileData.isEmpty())
		return false;

	setMode(Mode::Rich);
	// Relative image paths resolve against the document URL
	_richView->document()->setMetaInformation(QTextDocument::DocumentUrl, QUrl::fromLocalFile(_sourceFilePath).toString());
	_richView->setHtml(result.text);
	setViewAsAction(_htmlAction);
	return true;
}

bool CTextViewerWindow::asMarkdown(const QByteArray& fileData)
{
	const auto result = decodeUnicodeText(fileData);
	if (!result)
		return false;

	encodingChanged(result->encoding, result->language);
	setMode(Mode::Rich);
	_richView->document()->setMetaInformation(QTextDocument::DocumentUrl, QUrl::fromLocalFile(_sourceFilePath).toString());
	_richView->setMarkdown(result->text);
	setViewAsAction(_markdownAction);
	return true;
}

bool CTextViewerWindow::asHexFast(const QByteArray& fileData)
{
	setMode(Mode::Lightning);
	_lightningViewer->setData(fileData);
	encodingChanged(tr("none - viewing raw data"));
	setViewAsAction(_hexAction);
	return true;
}

void CTextViewerWindow::redecodeCurrentFile(bool (CTextViewerWindow::*decoder)(const QByteArray&, bool))
{
	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData)
	{
		updateViewAsCheck();
		return;
	}

	// Null when the initial load failed: _sourceFilePath is set before the file is read, so an action can arrive with no viewer built
	QAbstractScrollArea* const scrollArea = viewer().widget;
	const int scrollPosition = scrollArea ? scrollArea->verticalScrollBar()->value() : 0;

	const Mode modeBefore = _currentMode;
	if (!(this->*decoder)(*textData, _currentMode == Mode::Lightning))
	{
		updateViewAsCheck();
		return;
	}

	// A decoder can move Rich to Source, which destroys scrollArea; and each viewer scrolls in units of its own
	if (scrollArea && _currentMode == modeBefore)
		scrollArea->verticalScrollBar()->setValue(scrollPosition);
}

void CTextViewerWindow::renderCurrentFile(bool (CTextViewerWindow::*renderer)(const QByteArray&))
{
	const std::optional<QByteArray> textData = readFileAndReportErrors();
	if (!textData || !(this->*renderer)(*textData))
		updateViewAsCheck();
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

std::optional<CTextEncodingDetector::DecodedText> CTextViewerWindow::decodeUnicodeText(const QByteArray& textData)
{
	if (const CTextEncodingDetector::DecodedText decodedText = CTextEncodingDetector::decodeUtfBom(textData); !decodedText.encoding.isEmpty())
		return decodedText;

	return CTextEncodingDetector::DecodedText{QString::fromUtf8(textData), "UTF-8", {}, 0.0};
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

CTextViewerWindow::ViewerOps CTextViewerWindow::viewer() const
{
	// One binding serves both document views: same calls, no base class that declares them
	const auto documentViewOps = [](auto* view) -> ViewerOps {
		// A miss on both passes leaves the cursor and the scroll as they were
		const auto findWrappingAround = [view](const auto& expression, QTextDocument::FindFlags flags) {
			if (view->find(expression, flags))
				return FindResult::Found;

			const QTextCursor cursor = view->textCursor();
			const int horizontalScroll = view->horizontalScrollBar()->value();
			const int verticalScroll = view->verticalScrollBar()->value();

			view->moveCursor(flags.testFlag(QTextDocument::FindBackward) ? QTextCursor::End : QTextCursor::Start);
			if (view->find(expression, flags))
				return FindResult::FoundAfterWrapAround;

			view->setTextCursor(cursor);
			view->horizontalScrollBar()->setValue(horizontalScroll);
			view->verticalScrollBar()->setValue(verticalScroll);
			return FindResult::NotFound;
		};

		// Searches the document, not the view: the view's cursor and scroll stay as they are
		const auto countMatches = [view](const auto& expression, QTextDocument::FindFlags flags, QDeadlineTimer deadline, bool highlight) {
			constexpr qsizetype maxHighlightedMatches = 10'000; // QPlainTextEdit walks every extra selection for each block it paints

			flags.setFlag(QTextDocument::FindBackward, false);
			const QTextCursor selection = view->textCursor();

			const QColor highlightFill = ColorUtils::searchMatchFill(view->palette(), ColorUtils::SearchMatch::Other);
			const QColor paletteText = view->palette().color(QPalette::Text);
			QTextCharFormat highlightFormat;
			highlightFormat.setBackground(highlightFill);
			if (const QColor text = ColorUtils::readableTextOn(highlightFill, paletteText); text != paletteText)
				highlightFormat.setForeground(text); // Otherwise the syntax colours stay
			QList<QTextEdit::ExtraSelection> highlights;

			MatchCount count;
			for (int from = 0;;)
			{
				const QTextCursor match = view->document()->find(expression, from, flags);
				if (match.isNull())
				{
					count.complete = true;
					break;
				}

				if (match.hasSelection())
				{
					++count.total;
					if (selection.hasSelection() && match.selectionStart() == selection.selectionStart())
						count.number = count.total;
					if (highlight && highlights.size() < maxHighlightedMatches)
						highlights.push_back({ match, highlightFormat });
				}

				from = match.selectionEnd() + (match.hasSelection() ? 0 : 1); // An empty regex match is found again where it ends
				if (deadline.hasExpired())
					break;
			}

			view->setExtraSelections(highlights);
			return count;
		};

		return {
			view,
			findWrappingAround,
			findWrappingAround,
			countMatches,
			countMatches,
			[view] { view->setExtraSelections({}); },
			[view](bool wrap) { view->setWordWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap); }
		};
	};

	if (_sourceView)
		return documentViewOps(_sourceView.get());

	if (_richView)
		return documentViewOps(_richView.get());

	if (auto* const view = _lightningViewer.get())
	{
		const auto findWrappingAround = [view](const auto& expression, QTextDocument::FindFlags flags) { return view->find(expression, flags, /*wrapAround=*/true); };
		const auto countMatches = [view](const auto& expression, QTextDocument::FindFlags flags, QDeadlineTimer deadline, bool highlight) {
			view->setCountedMatchesHighlighted(highlight);
			return view->countMatches(expression, flags, deadline);
		};

		return {
			view,
			findWrappingAround,
			findWrappingAround,
			countMatches,
			countMatches,
			[view] { view->setCountedMatchesHighlighted(false); },
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

	// Every content change passes through here
	_findBar->clearStatus();

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
			_centralLayout->insertWidget(0, _sourceView.get(), 1);
		}
		break;

	case Mode::Rich:
		if (!_richView)
		{
			_richView = std::make_unique<QTextEdit>(this);
			initDocumentView(*_richView);
			_richView->setAcceptRichText(true);
			_centralLayout->insertWidget(0, _richView.get(), 1);
		}
		break;

	case Mode::Lightning:
		if (!_lightningViewer)
		{
			_lightningViewer = std::make_unique<CLightningFastViewerWidget>(this);
			setFontSize(*_lightningViewer);
			_centralLayout->insertWidget(0, _lightningViewer.get(), 1);
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
	if (text.size() <= maxSizeForSyntaxHighlighting)
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

void CTextViewerWindow::setViewAsAction(QAction* action)
{
	_viewAsAction = action;
	updateViewAsCheck();
}

void CTextViewerWindow::updateViewAsCheck()
{
	if (_viewAsAction)
		_viewAsAction->setChecked(true);
	else if (QAction* const checkedAction = _viewAsGroup->checkedAction())
		checkedAction->setChecked(false);
}
