#pragma once

#include "plugininterface/cpluginwindow.h"
#include "ctextencodingdetector.h"

#include "ui_ctextviewerwindow.h"

#include <memory>
#include <optional>

class CTextEditWithImageSupport;
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
	bool asDetectedAutomatically();
	bool asSystemDefault();
	bool asAscii();
	bool asUtf8();
	bool asUtf16();
	bool asHtml();
	bool asMarkdown();

	[[nodiscard]] std::optional<QByteArray> readFileAndReportErrors();
	[[nodiscard]] std::optional<CTextEncodingDetector::DecodedText> decodeText(const QByteArray& textData);

	void find();
	void findNext();

	bool readSource(QByteArray& data) const;

	void encodingChanged(const QString& encoding, const QString& language = QString());

	void setLineWrap(bool wrap);

	void setupFindDialog();

private:
	void setTextAndApplyHighlighter(const QString& text);

private:
	QString _sourceFilePath;
	QString _mimeType;

	CTextEditWithImageSupport* _textBrowser = nullptr;
	CFindDialog* _findDialog = nullptr;
	QLabel* _encodingLabel = nullptr;

	std::unique_ptr<QSyntaxHighlighter> _syntaxHighlighter;
	std::unique_ptr<Qutepart::Theme> _theme;
};
