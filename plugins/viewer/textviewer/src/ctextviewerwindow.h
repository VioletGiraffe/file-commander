#pragma once

#include "plugininterface/cpluginwindow.h"
#include "ctextencodingdetector.h"

#include "ui_ctextviewerwindow.h"

#include <optional>

class QLabel;
class CTextEditWithLineNumbers;
class CFindDialog;

class CTextViewerWindow final : public CPluginWindow, private Ui::CTextViewerWindow
{
public:
	explicit CTextViewerWindow(QWidget* parent = nullptr) noexcept;

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
	QString _sourceFilePath;

	CTextEditWithLineNumbers* _textBrowser = nullptr;
	CFindDialog* _findDialog = nullptr;
	QLabel* _encodingLabel = nullptr;
};
