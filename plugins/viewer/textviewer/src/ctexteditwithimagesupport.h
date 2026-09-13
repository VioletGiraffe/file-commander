#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QTextEdit>
#include <QUrl>
#include <QVariant>
RESTORE_COMPILER_WARNINGS

class CTextEditWithImageSupport final : public QTextEdit
{
	struct Downloader;

public:
	using QTextEdit::QTextEdit;

protected:
	QVariant loadResource(int type, const QUrl &name) override;

private:
	Downloader* _downloader = nullptr;
};
