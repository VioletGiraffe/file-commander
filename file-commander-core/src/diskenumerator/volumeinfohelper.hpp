#pragma once

#if defined __linux__ || defined __APPLE__

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <sys/param.h>
#include <sys/mount.h>
#include <errno.h>

inline struct statfs64 volumeInfoForPath(const QString& path)
{
	struct statfs64 info;
	memset(&info, 0, sizeof(info));
	if (statfs64(path.toLocal8Bit().data(), &info) != 0)
		qInfo() << "Error occurred while calling statvfs64 for" << path << "\n" << strerror(errno);

	return info;
}
#endif
