#pragma once

#if defined __linux__ || defined __APPLE__ || defined __FreeBSD__

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <sys/param.h>
#include <sys/mount.h>
#include <errno.h>
#include <string.h>

#ifdef __linux__
#include <sys/vfs.h>
#endif

#include <optional>
#include <vector>

// Empty if statfs() fails; the failure's message is appended to failureMessages
inline std::optional<struct statfs> volumeInfoForPath(const QString& path, std::vector<QString>& failureMessages)
{
	struct statfs info;
	if (statfs(path.toLocal8Bit().data(), &info) != 0)
	{
		const int error = errno;
		failureMessages.push_back(QStringLiteral("statfs() failed for ") + path + QStringLiteral(": ") + QString::fromLocal8Bit(strerror(error)));
		return std::nullopt;
	}

	return info;
}
#endif
