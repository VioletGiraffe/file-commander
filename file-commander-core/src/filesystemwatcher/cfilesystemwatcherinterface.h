#pragma once

#include "compiler/compiler_warnings_control.h"
#include "container/std_container_helpers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <mutex>

namespace detail {

class CFileSystemWatcherInterface
{
public:
	using ChangeDetectedCallback = std::function<void()>;

	virtual ~CFileSystemWatcherInterface() = default;

	// Callbacks must be thread-safe!
	void addCallback(ChangeDetectedCallback callback);
	virtual bool setPathToWatch(const QString& path) = 0;

protected:
	void notifySubscribers();

protected:
	std::recursive_mutex _mutex;
	QString _pathToWatch;

private:
	std::vector<ChangeDetectedCallback> _callbacks;
};

}
