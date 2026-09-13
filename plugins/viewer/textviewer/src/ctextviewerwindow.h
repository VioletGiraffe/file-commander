#pragma once

#include "plugininterface/cpluginwindow.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "ctextencodingdetector.h"


DISABLE_COMPILER_WARNINGS
#include "ui_ctextviewerwindow.h"

#include <QTextDocument>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <memory>
#include <optional>

class CPlainTextEditWithLineNumbers;
class CTextEditWithImageSupport;
class CLightningFastViewerWidget;
class CFindDialog;

class QAbstractScrollArea;
class QLabel;
class QRegularExpression;

namespace Qutepart {
	class Theme;
	class SyntaxHighlighter;
	QString chooseLanguageXmlFileName(const QString& mimeType, const QString& languageName, const QString& sourceFilePath, const QString& firstLine);
}

class CTextViewerWindow final : public CPluginWindow, private Ui::CTextViewerWindow
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

	[[nodiscard]] std::optional<QByteArray> readFileAndReportErrors() const;
	[[nodiscard]] std::optional<CTextEncodingDetector::DecodedText> decodeText(const QByteArray& textData);
	[[nodiscard]] std::optional<CTextEncodingDetector::DecodedText> decodeUnicodeText(const QByteArray& textData);

	void find();
	void findNext();

	[[nodiscard]] bool readSource(QByteArray& data) const;

	void encodingChanged(const QString& encoding, const QString& language = QString());

	void setLineWrap(bool wrap);

	void setupFindDialog();

private:
	// Source and Rich are separate widgets: rendering HTML and Markdown needs QTextEdit, and plain text is far cheaper without it
	enum class Mode {Lightning, Source, Rich};
	void setMode(Mode mode);

	// What the window needs of whichever viewer is live. The three share no base below QAbstractScrollArea,
	// so the calls are bound per viewer rather than dispatched.
	struct ViewerOps
	{
		QAbstractScrollArea* widget = nullptr; // Null until the first load, and then the only member worth testing

		std::function<bool (const QString&, QTextDocument::FindFlags)> findText;
		std::function<bool (const QRegularExpression&, QTextDocument::FindFlags)> findRegex;
		std::function<void ()> moveToStart;
		std::function<void ()> moveToEnd;
		std::function<qsizetype ()> cursorPosition; // -1 where the viewer has no cursor yet
		std::function<void (bool)> setWordWrap;
	};

	[[nodiscard]] ViewerOps viewer() const;

	void setTextAndApplyHighlighter(const QString& text);
	void resetHighlighter();

	void updateContentTypeLabel();

private:
	QString _sourceFilePath;
	QString _mimeType;

	std::unique_ptr<CPlainTextEditWithLineNumbers> _sourceView;
	std::unique_ptr<CTextEditWithImageSupport> _richView;
	CFindDialog* _findDialog = nullptr;
	QLabel* _encodingLabel = nullptr;
	QLabel* _contentTypeLabel = nullptr;
	QLabel* _infoLabel = nullptr;

	Qutepart::SyntaxHighlighter* _highlighter = nullptr;
	std::unique_ptr<Qutepart::Theme> _theme;

	std::unique_ptr<CLightningFastViewerWidget> _lightningViewer;

	Mode _currentMode = Mode::Source;
};
