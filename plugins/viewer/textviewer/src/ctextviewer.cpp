#include "ctextviewer.h"


// Submodule includes
#include "qtcore_helpers/qt_helpers.hpp"
#include "theme/colorutils.h"
#include "widgets/cfindbar.h"
#include "widgets/clightningfastviewer.h"
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
#include <QFile>
#include <QFontMetrics>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMimeDatabase>
#include <QPalette>
#include <QRegularExpression>
#include <QScrollBar>
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

[[nodiscard]] static QString unreadableFileMessage(const QString& path)
{
	return CTextViewer::tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(path);
}

CTextViewer::CTextViewer() noexcept
{
	setWindowIcon(QIcon{ QStringLiteral(":/main_icon") }); // A hosting window follows it

	_layout = new QVBoxLayout{ this };
	_layout->setContentsMargins(0, 0, 0, 0);
	_layout->setSpacing(0);

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
	_layout->addWidget(_findBar);

	const auto addMenuAction = [this](QMenu* menu, const QString& text, const QString& shortcut, auto onTriggered) {
		QAction* const action = menu->addAction(text);
		action->setShortcut(QKeySequence{ shortcut });
		CR() = connect(action, &QAction::triggered, this, onTriggered);
		return action;
	};

	auto* const editMenu = new QMenu{ tr("&Edit"), this };
	editMenu->addActions(_findBar->findActions());
	addMenu(editMenu);

	auto* const viewMenu = new QMenu{ tr("&View"), this };
	addMenuAction(viewMenu, tr("Auto &detect encoding"), "1", [this] { redecodeCurrentFile(&CTextViewer::asDetectedAutomatically); });

	_viewAsGroup = new QActionGroup{ this };
	const auto addViewAsAction = [&](const QString& text, const QString& shortcut, auto onTriggered) {
		QAction* const action = addMenuAction(viewMenu, text, shortcut, onTriggered);
		action->setCheckable(true);
		_viewAsGroup->addAction(action);
		return action;
	};

	_asciiAction = addViewAsAction(tr("&ASCII"), "2", [this] { redecodeCurrentFile(&CTextViewer::asAscii); });
	_systemLocaleAction = addViewAsAction(tr("&System locale"), "3", [this] { redecodeCurrentFile(&CTextViewer::asSystemDefault); });
	_utf8Action = addViewAsAction(tr("UTF-&8"), "4", [this] { redecodeCurrentFile(&CTextViewer::asUtf8); });
	_utf16Action = addViewAsAction(tr("UTF-1&6"), "5", [this] { redecodeCurrentFile(&CTextViewer::asUtf16); });
	_hexAction = addViewAsAction(tr("&Hex"), "6", [this] { renderCurrentFile(&CTextViewer::asHexFast); });
	_htmlAction = addViewAsAction(tr("H&TML"), "7", [this] { renderCurrentFile(&CTextViewer::asHtml); });
	_markdownAction = addViewAsAction(tr("&Markdown"), "8", [this] { renderCurrentFile(&CTextViewer::asMarkdown); });

	viewMenu->addSeparator();
	_lineWrapAction = addMenuAction(viewMenu, tr("&Line wrap"), "Shift+W", &CTextViewer::setLineWrap);
	_lineWrapAction->setCheckable(true);
	_lineWrapAction->setChecked(true); // Wrap by default
	addMenu(viewMenu);

	_encodingLabel = new QLabel(this);
	_contentTypeLabel = new QLabel(this);
	_infoLabel = new QLabel(this);
	_encodingLabel->setContentsMargins(4, 0, 4, 0);
	_contentTypeLabel->setContentsMargins(4, 0, 4, 0);
	_infoLabel->setContentsMargins(4, 0, 4, 0);

	auto* status = new QStatusBar{ this };
	status->setSizeGripEnabled(false);
	status->addWidget(_encodingLabel);
	status->addWidget(_contentTypeLabel);
	status->addWidget(_infoLabel);
	_layout->addWidget(status);

	_infoLabel->setVisible(false);
}

CTextViewer::~CTextViewer() = default;

// The syntax highlighter is what this limits: past it the Lightning viewer shows plain text instead
constexpr qsizetype maxSizeForSyntaxHighlighting = 3'000'000; // 3 MB of .c code = 800 ms on Core i5-12500
// detect() samples 256 KB whatever the input size, but the winning codec still decodes the whole file
constexpr qsizetype maxSizeForEncodingDetection = 25'000'000;

QString CTextViewer::tryLoadFile(const QString& path)
{
	_sourceFilePath = path;
	_mimeType = QMimeDatabase().mimeTypeForFile(_sourceFilePath, QMimeDatabase::MatchContent).name();

	try
	{
		QByteArray textData;
		if (!readSource(textData))
			return unreadableFileMessage(path);

		const auto decode = [&]() -> bool {
			const qsizetype dataSize = textData.size();

			// Binary content, and anything too large to decode, go to the fast viewer as Latin-1: every byte maps to a
			// character. Size first: isBinaryContent() decodes, so it must not see a file this branch already rejects.
			if (dataSize > maxSizeForEncodingDetection || isBinaryContent(textData))
				return asAscii(textData, true);

			const bool useFastMode = dataSize > maxSizeForSyntaxHighlighting;

			if (!useFastMode && (_sourceFilePath.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive) || _sourceFilePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)))
				return asHtml(textData);
			else if (_sourceFilePath.endsWith(".md", Qt::CaseInsensitive) && !useFastMode)
				return asMarkdown(textData);
			else if (_mimeType.contains("text") || _mimeType.isEmpty() || _mimeType.contains("octet-stream"))
				return asDetectedAutomatically(textData, useFastMode);
			else
				return asUtf8(textData, useFastMode) || asAscii(textData, useFastMode);
		};

		return decode() ? QString{} : tr("Failed to decode the file\n\n%1").arg(path);
	}
	catch (const std::bad_alloc&)
	{
		return tr("The file is too large to handle.");
	}
}

bool CTextViewer::asDetectedAutomatically(const QByteArray& fileData, bool useFastMode)
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

bool CTextViewer::asSystemDefault(const QByteArray& fileData, bool useFastMode)
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

bool CTextViewer::asAscii(const QByteArray& fileData, bool useFastMode)
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

bool CTextViewer::asUtf8(const QByteArray& fileData, bool useFastMode)
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

bool CTextViewer::asUtf16(const QByteArray& fileData, bool useFastMode)
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

bool CTextViewer::asHtml(const QByteArray& fileData)
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

bool CTextViewer::asMarkdown(const QByteArray& fileData)
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

bool CTextViewer::asHexFast(const QByteArray& fileData)
{
	setMode(Mode::Lightning);
	_lightningViewer->setData(fileData);
	encodingChanged(tr("none - viewing raw data"));
	setViewAsAction(_hexAction);
	return true;
}

void CTextViewer::redecodeCurrentFile(bool (CTextViewer::*decoder)(const QByteArray&, bool))
{
	const std::optional<QByteArray> textData = readSourceOrShowError();
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

	hideLoadError();
	// A decoder can move Rich to Source, which destroys scrollArea; and each viewer scrolls in units of its own
	if (scrollArea && _currentMode == modeBefore)
		scrollArea->verticalScrollBar()->setValue(scrollPosition);
}

void CTextViewer::renderCurrentFile(bool (CTextViewer::*renderer)(const QByteArray&))
{
	const std::optional<QByteArray> textData = readSourceOrShowError();
	if (!textData || !(this->*renderer)(*textData))
		updateViewAsCheck();
	else
		hideLoadError();
}

std::optional<QByteArray> CTextViewer::readSourceOrShowError()
{
	QByteArray textData;
	if (!readSource(textData))
	{
		showLoadError(unreadableFileMessage(_sourceFilePath));
		return {};
	}

	return std::optional{ std::move(textData) };
}

std::optional<CTextEncodingDetector::DecodedText> CTextViewer::decodeUnicodeText(const QByteArray& textData)
{
	if (const CTextEncodingDetector::DecodedText decodedText = CTextEncodingDetector::decodeUtfBom(textData); !decodedText.encoding.isEmpty())
		return decodedText;

	return CTextEncodingDetector::DecodedText{QString::fromUtf8(textData), "UTF-8", {}, 0.0};
}

bool CTextViewer::readSource(QByteArray& textData) const
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

void CTextViewer::encodingChanged(const QString& encoding, const QString& language)
{
	QString message;
	if (!encoding.isEmpty())
		message = tr("Text encoding: ") % encoding;
	if (!language.isEmpty())
		message = message % ", " % tr("language: ") % language;

	_encodingLabel->setText(message);
}

void CTextViewer::setLineWrap(bool wrap)
{
	if (const ViewerOps v = viewer(); v.widget)
		v.setWordWrap(wrap);
}

CTextViewer::ViewerOps CTextViewer::viewer() const
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

void CTextViewer::setMode(Mode mode)
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
			_layout->insertWidget(0, _sourceView.get(), 1);
		}
		break;

	case Mode::Rich:
		if (!_richView)
		{
			_richView = std::make_unique<QTextEdit>(this);
			initDocumentView(*_richView);
			_richView->setAcceptRichText(true);
			_layout->insertWidget(0, _richView.get(), 1);
		}
		break;

	case Mode::Lightning:
		if (!_lightningViewer)
		{
			_lightningViewer = std::make_unique<CLightningFastViewerWidget>(this);
			setFontSize(*_lightningViewer);
			_layout->insertWidget(0, _lightningViewer.get(), 1);
		}

		_infoLabel->setText(tr("FAST MODE! Encoding detection didn't run, you can trigger it manually"));
		break;
	}

	_infoLabel->setVisible(mode == Mode::Lightning);

	// Apply line wrap setting
	setLineWrap(_lineWrapAction->isChecked());
}

void CTextViewer::setTextAndApplyHighlighter(const QString& text)
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

void CTextViewer::resetHighlighter()
{
	if (!_highlighter)
		return;

	_highlighter->setDocument(nullptr);
	delete _highlighter;
	_highlighter = nullptr;
	_theme.reset();
}

void CTextViewer::updateContentTypeLabel()
{
	QString format = _highlighter ? _highlighter->languageName() : QString{};
	if (format.isEmpty())
		format = _mimeType;

	_contentTypeLabel->setText(tr("Content format: ") + format);
}

void CTextViewer::setViewAsAction(QAction* action)
{
	_viewAsAction = action;
	updateViewAsCheck();
}

void CTextViewer::updateViewAsCheck()
{
	if (_viewAsAction)
		_viewAsAction->setChecked(true);
	else if (QAction* const checkedAction = _viewAsGroup->checkedAction())
		checkedAction->setChecked(false);
}
