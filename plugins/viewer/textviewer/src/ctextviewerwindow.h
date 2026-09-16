#pragma once

#include "plugininterface/cpluginwindow.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "ctextencodingdetector.h"
#include "widgets/findresult.h"


DISABLE_COMPILER_WARNINGS
#include <QDeadlineTimer>
#include <QTextDocument>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <memory>
#include <optional>

class CPlainTextEditWithLineNumbers;
class CLightningFastViewerWidget;
class CFindBar;

class QAbstractScrollArea;
class QAction;
class QActionGroup;
class QLabel;
class QRegularExpression;
class QTextEdit;
class QVBoxLayout;

namespace Qutepart {
	class Theme;
	class SyntaxHighlighter;
	QString chooseLanguageXmlFileName(const QString& mimeType, const QString& languageName, const QString& sourceFilePath, const QString& firstLine);
}

class CTextViewerWindow final : public CPluginWindow
{
public:
	explicit CTextViewerWindow(QWidget* parent = nullptr) noexcept;
	~CTextViewerWindow() override;

	bool loadTextFile(const QString& file);

private:
	bool asDetectedAutomatically(const QByteArray& fileData, bool useFastMode);
	bool asSystemDefault(const QByteArray& fileData, bool useFastMode);
	bool asAscii(const QByteArray& fileData, bool useFastMode);
	bool asUtf8(const QByteArray& fileData, bool useFastMode);
	bool asUtf16(const QByteArray& fileData, bool useFastMode);
	bool asHtml(const QByteArray& fileData);
	bool asMarkdown(const QByteArray& fileData);

	bool asHexFast(const QByteArray& fileData);

	// Re-runs one of the text decoders above over the file already open, keeping the scroll position: the same content, read differently
	void redecodeCurrentFile(bool (CTextViewerWindow::*decoder)(const QByteArray&, bool));
	void renderCurrentFile(bool (CTextViewerWindow::*renderer)(const QByteArray&));

	[[nodiscard]] std::optional<QByteArray> readFileAndReportErrors() const;
	[[nodiscard]] std::optional<CTextEncodingDetector::DecodedText> decodeUnicodeText(const QByteArray& textData);

	[[nodiscard]] bool readSource(QByteArray& data) const;

	void encodingChanged(const QString& encoding, const QString& language = QString());

	void setLineWrap(bool wrap);

private:
	// Source and Rich are separate widgets: rendering HTML and Markdown needs QTextEdit, and plain text is far cheaper without it
	enum class Mode {Lightning, Source, Rich};
	void setMode(Mode mode);

	// What the window needs of whichever viewer is live. The three share no base below QAbstractScrollArea,
	// so the calls are bound per viewer rather than dispatched.
	struct ViewerOps
	{
		QAbstractScrollArea* widget = nullptr; // Null until the first load, and then the only member worth testing

		// Both wrap around at either end
		std::function<FindResult (const QString&, QTextDocument::FindFlags)> findText;
		std::function<FindResult (const QRegularExpression&, QTextDocument::FindFlags)> findRegex;
		// Both count the matches a forward find steps through, until the deadline; 'highlight' paints them until clearHighlights
		std::function<MatchCount (const QString&, QTextDocument::FindFlags, QDeadlineTimer, bool highlight)> countText;
		std::function<MatchCount (const QRegularExpression&, QTextDocument::FindFlags, QDeadlineTimer, bool highlight)> countRegex;
		std::function<void ()> clearHighlights;
		std::function<void (bool)> setWordWrap;
	};

	[[nodiscard]] ViewerOps viewer() const;

	void setTextAndApplyHighlighter(const QString& text);
	void resetHighlighter();

	void updateContentTypeLabel();

	void setViewAsAction(QAction* action);
	// Qt checks a clicked View menu entry before its handler runs, so a handler that fails must call this
	void updateViewAsCheck();

private:
	QString _sourceFilePath;
	QString _mimeType;

	std::unique_ptr<CPlainTextEditWithLineNumbers> _sourceView;
	std::unique_ptr<QTextEdit> _richView;
	QVBoxLayout* _centralLayout = nullptr; // The live viewer above _findBar
	CFindBar* _findBar = nullptr;
	QLabel* _encodingLabel = nullptr;
	QLabel* _contentTypeLabel = nullptr;
	QLabel* _infoLabel = nullptr;

	QActionGroup* _viewAsGroup = nullptr;
	QAction* _viewAsAction = nullptr; // The _viewAsGroup entry matching the displayed content
	QAction* _asciiAction = nullptr;
	QAction* _systemLocaleAction = nullptr;
	QAction* _utf8Action = nullptr;
	QAction* _utf16Action = nullptr;
	QAction* _hexAction = nullptr;
	QAction* _htmlAction = nullptr;
	QAction* _markdownAction = nullptr;
	QAction* _lineWrapAction = nullptr;

	Qutepart::SyntaxHighlighter* _highlighter = nullptr;
	std::unique_ptr<Qutepart::Theme> _theme;

	std::unique_ptr<CLightningFastViewerWidget> _lightningViewer;

	Mode _currentMode = Mode::Source;
};
