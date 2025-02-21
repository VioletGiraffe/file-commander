#pragma once
#include "widgets/ctexteditwithlinenumbers.h"

class CTextEditWithImageSupport final : public CTextEditWithLineNumbers
{
	struct Downloader;

public:
	using CTextEditWithLineNumbers::CTextEditWithLineNumbers;

protected:
	QVariant loadResource(int type, const QUrl &name) override;

private:
	Downloader* _downloader = nullptr;
};
