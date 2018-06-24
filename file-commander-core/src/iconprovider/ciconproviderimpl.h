#pragma once

#include "settings.h"
#include "settings/csettings.h"
#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#ifndef _WIN32
#include <QFileIconProvider>
#endif
RESTORE_COMPILER_WARNINGS

class QIcon;

#ifdef _WIN32

class CIconProviderImpl
{
public:
	QIcon iconFor(const CFileSystemObject& object);
	void settingsChanged();

private:
	bool _showOverlayIcons = false;
};

#else

class CIconProviderImpl
{
public:
	QIcon iconFor(const CFileSystemObject& object);
	void settingsChanged();

private:
	bool _showOverlayIcons = false;
	QFileIconProvider _provider;
};

#endif

