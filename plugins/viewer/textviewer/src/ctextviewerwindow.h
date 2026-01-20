#pragma once

#include "plugininterface/cpluginwindow.h"
#include "ctextencodingdetector.h"

#include "ui_ctextviewerwindow.h"

#include <memory>
#include <optional>

class CTextEditWithImageSupport;
class CLightningFastViewerWidget;
class CFindDialog;

class QLabel;
class QSyntaxHighlighter;

namespace Qutepart {
	class Theme;
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

	[[nodiscard]] std::optional<QByteArray> readFileAndReportErrors() const;
	[[nodiscard]] std::optional<CTextEncodingDetector::DecodedText> decodeText(const QByteArray& textData);

	void find();
	void findNext();

	[[nodiscard]] bool readSource(QByteArray& data) const;

	void encodingChanged(const QString& encoding, const QString& language = QString());

	void setLineWrap(bool wrap);

	void setupFindDialog();

private:
	enum class Mode {Lightning, Full};
	void setMode(Mode mode);

	void setTextAndApplyHighlighter(const QString& text);

private:
	QString _sourceFilePath;
	QString _mimeType;

	std::unique_ptr<CTextEditWithImageSupport> _textBrowser;
	CFindDialog* _findDialog = nullptr;
	QLabel* _encodingLabel = nullptr;

	std::unique_ptr<Qutepart::Theme> _theme;

	std::unique_ptr<CLightningFastViewerWidget> _lightningViewer;

	Mode _currentMode = Mode::Full;
};
