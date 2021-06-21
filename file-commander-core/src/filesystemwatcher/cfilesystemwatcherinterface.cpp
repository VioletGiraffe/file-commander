#include "cfilesystemwatcherinterface.h"

void detail::CFileSystemWatcherInterface::addCallback(ChangeDetectedCallback callback)
{
	std::lock_guard lock{ _mutex };
	_callbacks.emplace_back(std::move(callback));
}

void detail::CFileSystemWatcherInterface::notifySubscribers()
{
	std::unique_lock lock{ _mutex };
	auto callbacksLocal = _callbacks;
	lock.unlock();

	for (auto&& callback : callbacksLocal)
		callback();
}

