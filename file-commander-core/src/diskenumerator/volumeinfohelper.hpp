#pragma once

#if defined __linux__ || defined __APPLE__

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <sys/statvfs.h>
#include <errno.h>

inline struct statvfs64 volumeInfoForPath(const QString& path)
{
	struct statvfs64 info;
	memset(&info, 0, sizeof(info));
	if (statvfs64(path.toLocal8Bit().data(),&info) != 0)
		qInfo() << "Error occurred while calling statvfs64:\n" << strerror(errno);

	return info;
}
#endif
