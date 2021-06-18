#include "cfilesystemwatcherinterface.h"

void detail::CFileSystemWatcherInterface::addCallback(ChangeDetectedCallback callback)
{
	_callbacks.emplace_back(std::move(callback));
}

void detail::CFileSystemWatcherInterface::notifySubscribers()
{
	for (const auto& callback : _callbacks)
		callback();
}

