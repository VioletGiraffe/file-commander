#pragma once

#if defined __linux__ || defined __APPLE__ || defined __FreeBSD__

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <sys/param.h>
#include <sys/mount.h>
#include <errno.h>

#ifdef __linux__
#include <sys/vfs.h> // statfs64
#elif defined __FreeBSD__
#define statfs64 statfs
#endif

inline struct statfs volumeInfoForPath(const QString& path)
{
	struct statfs info;
	memset(&info, 0, sizeof(info));
	if (statfs(path.toLocal8Bit().data(), &info) != 0)
		qInfo() << "Error occurred while calling statfs for" << path << "\n" << strerror(errno);

	return info;
}
#endif
