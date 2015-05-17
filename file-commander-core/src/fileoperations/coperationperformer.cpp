#include "coperationperformer.h"
#include "../filesystemhelperfunctions.h"

#include <assert.h>
#include <functional>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

COperationPerformer::COperationPerformer(Operation operation, std::vector<CFileSystemObject> source, QString destination) :
	_source(source),
	_destFileSystemObject(toPosixSeparators(destination)),
	_op(operation),
	_paused(false),
	_inProgress(false),
	_finished(false),
	_cancelRequested(false),
	_userResponse(urNone),
	_observer(0)
{
}

COperationPerformer::~COperationPerformer()
{
	cancel();

	// TODO: this doesn't look thread-safe
	assert(_thread.joinable());
	_thread.join();
}

void COperationPerformer::setWatcher(CFileOperationObserver *watcher)
{
	assert(watcher);
	_observer = watcher;
}

bool COperationPerformer::togglePause()
{
	_paused = !_paused;
	return _paused;
}

bool COperationPerformer::paused() const
{
	return _paused;
}

bool COperationPerformer::working() const
{
	return _inProgress;
}

bool COperationPerformer::done() const
{
	return _finished;
}

// User can supply a new name (not full path)
void COperationPerformer::userResponse(HaltReason haltReason, UserResponse response, QString newName)
{
	assert (_userResponse == urNone); // _userResponse should have been reset after being used
	_newName = newName;

	_userResponse = response;
	if (_userResponse == urSkipAll || _userResponse == urProceedWithAll)
		_globalResponses[haltReason] = response;
	_waitForResponseCondition.notify_one();
}

void COperationPerformer::start()
{
	_thread = std::thread(&COperationPerformer::threadFunc, this);
	while (!_inProgress) std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Waiting for thread to start, not sure if needed
}

void COperationPerformer::cancel()
{
	_cancelRequested = true;
}

void COperationPerformer::threadFunc()
{
	switch (_op)
	{
	case operationCopy:
		copyFiles();
		break;
	case operationMove:
		copyFiles();
		break;
	case operationDelete:
		deleteFiles();
		break;
	default:
		assert(false);
		return;
	}
}

void COperationPerformer::waitForResponse()
{
	std::unique_lock<std::mutex> lock (_waitForResponseMutex);
	_totalTimeElapsed.pause();
	while (_userResponse == urNone)
		_waitForResponseCondition.wait(lock);

	_totalTimeElapsed.resume();
}

void COperationPerformer::copyFiles()
{
	if (_source.empty())
	{
		finalize();
		return;
	}

	_inProgress = true;

	// If there's just one file to copy it is allowed to set a new file name as dest (C:/1.txt) instead of just the path (C:/)
	QString newFileName;
	if (_source.size() == 1 && _source.front().isFile() && !_destFileSystemObject.isDir())
		newFileName = _destFileSystemObject.fullName();

	_totalTimeElapsed.start();
	size_t currentItemIndex = 0;

	// Check if source and dest are on the same file system / disk drive, in which case moving is much simpler and faster
	// If the dest folder is empty, moving means renaming the root source folder / file, which is fast and simple
	if (_op == operationMove && (!_destFileSystemObject.exists() || _destFileSystemObject.isEmptyDir()) && _source.front().isMovableTo(_destFileSystemObject))
	{
		// TODO: Assuming that all sources are from the same drive / file system. Can that assumption ever be incorrect?
		for (auto it = _source.begin(); it != _source.end() && !_cancelRequested; _userResponse = urNone /* needed for normal operation of condition variable */)
		{
			if (it->isCdUp())
			{
				++it;
				++currentItemIndex;
				continue;
			}

			const auto result = it->moveAtomically(_destFileSystemObject.fullAbsolutePath(), newFileName);
			if (result != rcOk)
			{
				const auto response = getUserResponse(result == rcTargetAlreadyExists ? hrFileExists : hrUnknownError, *it, CFileSystemObject(QString(_destFileSystemObject.fullAbsolutePath() % "/" % (newFileName.isEmpty() ? it->fullName() : newFileName))), it->lastErrorMessage());
				// Handler is identical to that of the main loop
				// esp. for the case of hrFileExists
				if (response == urSkipThis || response == urSkipAll)
				{
					++it;
					++currentItemIndex;
					continue;
				}
				else if (response == urAbort)
				{
					finalize();
					return;
				}
				else if (response == urRename)
				{
					newFileName = _newName;
					continue;
				}
				else if (response == urRetry)
				{
					continue;
				}
				else
					assert(_userResponse == urProceedWithThis || _userResponse == urProceedWithAll);
			}

			++it;
			++currentItemIndex;
		}

		finalize();
		return;
	}

	uint64_t totalSize = 0, sizeProcessed = 0;
	const auto destination = flattenSourcesAndCalcDest(totalSize);
	assert(destination.size() == _source.size());

	std::vector<CFileSystemObject> dirsToCleanUp;

	for (auto it = _source.begin(); it != _source.end() && !_cancelRequested; _userResponse = urNone /* needed for normal operation of condition variable */)
	{
		if (it->isCdUp())
		{
			++it;
			++currentItemIndex;
			continue;
		}

		qDebug() << __FUNCTION__ << "Processing" << (it->isFile() ? "file" : "directory") << it->fullAbsolutePath();
		_observer->onCurrentFileChangedCallback(it->fullName());

		const QFileInfo& sourceFileInfo = it->qFileInfo();
		if (!sourceFileInfo.exists())
		{
			// Global response registered
			if (_globalResponses.count(hrFileDoesntExit) > 0)
			{
				if (_globalResponses[hrFileDoesntExit] == urSkipAll)
				{
					++it;
					++currentItemIndex;
					continue;
				}
			}

			_observer->onProcessHaltedCallback(hrFileDoesntExit, *it, CFileSystemObject(), QString());
			waitForResponse();
			if (_userResponse == urSkipThis || _userResponse == urSkipAll)
			{
				_userResponse = urNone;
				++it;
				++currentItemIndex;
				continue;
			}
			else if (_userResponse == urAbort)
			{
				_userResponse = urNone;
				finalize();
				return;
			}
			else
				assert (!"Unknown response");
			_userResponse = urNone;
		}

		QFileInfo destInfo(destination[currentItemIndex].absoluteFilePath(newFileName.isEmpty() ? it->fullName() : newFileName));
		if (destInfo.absoluteFilePath() == sourceFileInfo.absoluteFilePath())
		{
			++it;
			++currentItemIndex;
			continue;
		}

		if (destInfo.exists() && destInfo.isFile())
		{
			// Global response registered
			if (_globalResponses.count(hrFileExists) > 0)
			{
				if (_globalResponses[hrFileExists] == urSkipAll)
				{
					++it;
					++currentItemIndex;
					continue;
				}
			}
			else
			{
				CFileSystemObject destFile(destInfo);
				_observer->onProcessHaltedCallback(hrFileExists, *it, destFile, QString());
				waitForResponse();
				if (_userResponse == urSkipThis || _userResponse == urSkipAll)
				{
					_userResponse = urNone;
					++it;
					++currentItemIndex;
					continue;
				}
				else if (_userResponse == urAbort)
				{
					_userResponse = urNone;
					finalize();
					return;
				}
				else if (_userResponse == urRename)
				{
					assert(!_newName.isEmpty());
					_userResponse = urNone;
					newFileName = _newName;
					destFile = CFileSystemObject(destFile.parentDirPath() + "/" + _newName);
					if (destFile.exists())
						continue; // Retry
				}
				else if (_userResponse == urRetry)
				{
					_userResponse = urNone;
					continue;
				}
				else
					assert (((_userResponse == urProceedWithThis || _userResponse == urProceedWithAll) && _newName.isEmpty()) || (_userResponse == urRename && !_newName.isEmpty()) || (_globalResponses.count(hrFileExists) > 0 &&_globalResponses[hrFileExists] == urProceedWithAll));

				_userResponse = urNone;

				if (!destFile.isWriteable())
				{
					if (_globalResponses.count(hrDestFileIsReadOnly) <= 0 || _globalResponses[hrDestFileIsReadOnly] != urProceedWithAll)
					{
						_observer->onProcessHaltedCallback(hrSourceFileIsReadOnly, *it, CFileSystemObject(), QString());
						waitForResponse();
						if (_userResponse == urSkipThis || _userResponse == urSkipAll)
						{
							_userResponse = urNone;
							++it;
							++currentItemIndex;
							continue;
						}
						else if (_userResponse == urAbort)
						{
							_userResponse = urNone;
							finalize();
							return;
						}
						else if (_userResponse == urRetry)
						{
							_userResponse = urNone;
							continue;
						}
						else
							assert ((_userResponse == urProceedWithThis || _userResponse == urProceedWithAll) && _newName.isEmpty());

						if (!it->makeWritable())
						{
							qDebug() << "Error making file " << it->fullAbsolutePath() << " writable";
							continue;
						}

						_userResponse = urNone;
					}
				}

				if (destFile.remove() != rcOk)
				{
					qDebug() << "Error removing source file " << destFile.fullAbsolutePath() << ", error: " << destFile.lastErrorMessage();
					qDebug() << "Marking it as non-writable to prompt for the user decision";
					it->makeWritable(false);
					continue;
				}
			}
		}

		if (_op == operationCopy || _op == operationMove)
		{
			if (!destination[currentItemIndex].exists())
			{
				const bool ok = destination[currentItemIndex].mkpath(".");
				if (!ok)
				{
					_observer->onProcessHaltedCallback(hrFileExists, *it, CFileSystemObject(), QString());
					waitForResponse();
					if (_userResponse == urSkipThis || _userResponse == urSkipAll)
					{
						_userResponse = urNone;
						++it;
						++currentItemIndex;
						continue;
					}
					else if (_userResponse == urAbort)
					{
						_userResponse = urNone;
						finalize();
						return;
					}
					else if (_userResponse == urRetry)
					{
						_userResponse = urNone;
						continue;
					}
					else
					{
						Q_ASSERT(false);
						continue;
					}
				}
			}

			if (it->isFile())
			{
				static const int chunkSize = 5 * 1024 * 1024;
				const QString destPath = destination[currentItemIndex].absolutePath() + '/';
				FileOperationResultCode result = rcFail;

				// For speed calculation
				_fileTimeElapsed.start();
				do
				{
					if (_paused) // This code is not strictly thread-safe (the value of _paused can change between 'if' and 'while'), but in this context I'm OK with that
					{
						_fileTimeElapsed.pause();
						while (_paused)
							std::this_thread::sleep_for(std::chrono::milliseconds(100));

						_fileTimeElapsed.resume();
					}
					
					// TODO: add error checking, message displaying etc.!
					result = it->copyChunk(chunkSize, destPath, _newName.isEmpty() ? newFileName : _newName);
					// Error handling
					if (result != rcOk)
						break;

					const int totalPercentage = totalSize > 0 ? static_cast<int>((sizeProcessed + it->bytesCopied()) * 100 / totalSize) : 0;
					const int filePercentage = it->size() > 0 ? static_cast<int>(it->bytesCopied() * 100 / it->size()) : 0;
					const uint64_t speed = _fileTimeElapsed.elapsed() > 0 ? it->bytesCopied() * 1000 / _fileTimeElapsed.elapsed() : 0; // B/s
					_smoothSpeedCalculator = speed;
					_observer->onProgressChangedCallback(totalPercentage, currentItemIndex, _source.size(), filePercentage, _smoothSpeedCalculator.arithmeticMean());

					// TODO: why isn't this block at the start of 'do-while'?
					if (_cancelRequested)
					{
						if (it->cancelCopy() != rcOk)
							assert(false);
						result = rcOk;
						break;
					}
				}
				while (it->copyOperationInProgress());

				if (result != rcOk)
				{
					it->cancelCopy();
					qDebug() << "Error copying file " << it->fullAbsolutePath() << " to " << destPath + (_newName.isEmpty() ? newFileName : _newName) << ", error: " << it->lastErrorMessage();
					const auto action = getUserResponse(hrUnknownError, *it, CFileSystemObject(), it->lastErrorMessage());
					if (action == urSkipThis || action == urSkipAll)
					{
						_userResponse = urNone;
						++it;
						++currentItemIndex;
						continue;
					}
					else if (action == urAbort)
					{
						_userResponse = urNone;
						finalize();
						return;
					}
					else if (action == urRetry)
					{
						continue;
					}
					else
					{
						Q_ASSERT(false);
						continue;
					}

				}
				else if (_op == operationMove) // result == ok
				{
					NextAction nextAction;
					while ((nextAction = deleteItem(*it)) == naRetryOperation);

					switch (nextAction)
					{
					case naProceed:
					case naSkip:
						++it;
						++currentItemIndex;
						continue;
					case naRetryItem:
						continue;
					case naAbort:
						finalize();
						return;
					default:
						qDebug() << QString("Unexpected deleteItem() return value %1").arg(nextAction);
						Q_ASSERT(!"Unexpected deleteItem() return value");
						continue; // Retry
					}
				}
			}
			else if (it->isDir())
			{
				if (_op == operationMove)
				{
					if (it->isEmptyDir())
					{
						const auto result = it->remove();
						if (result != rcOk)
						{
							const auto action = getUserResponse(hrFailedToDelete, *it, CFileSystemObject(), it->lastErrorMessage());
							if (action == urSkipThis || action == urSkipAll)
							{
								_userResponse = urNone;
								++it;
								++currentItemIndex;
								continue;
							}
							else if (action == urAbort)
							{
								_userResponse = urNone;
								finalize();
								return;
							}
							else if (action == urRetry)
							{
								continue;
							}
							else
							{
								Q_ASSERT(false);
								continue;
							}
						}
					}
					else // not empty
						dirsToCleanUp.push_back(*it);
				}
				else if (_op == operationCopy)
				{
					CFileSystemObject destObject(destInfo);
					if (!destObject.exists())
					{
						if (!QDir(destObject.fullAbsolutePath()).mkdir("."))
						{
							const auto action = getUserResponse(hrCreatingFolderFailed, destObject, CFileSystemObject(), "");
							if (action == urSkipThis || action == urSkipAll)
							{
								++it;
								++currentItemIndex;
								continue;
							}
							else if (action == urAbort)
							{
								finalize();
								return;
							}
							else if (action == urRetry)
							{
								continue;
							}
							else
							{
								Q_ASSERT(false);
								continue;
							}
						}
					}
				}
			}
		}
		else
		{
			assert (!"Illegal op");
			break;
		}

		sizeProcessed += it->size();
		_newName.clear();

		++it;
		++currentItemIndex;
	}

	for (auto& dir: dirsToCleanUp)
		dir.remove();

	finalize();
}

void COperationPerformer::deleteFiles()
{
	_inProgress = true;
	uint64_t totalSize = 0;
	auto destination = flattenSourcesAndCalcDest(totalSize);
	assert (destination.size() == _source.size());

	size_t currentItemIndex = 0;
	for (auto it = _source.begin(); it != _source.end() && !_cancelRequested; currentItemIndex, _userResponse = urNone /* needed for normal condition variable operation */)
	{
		qDebug() << __FUNCTION__ << "deleting" << (it->isFile() ? "file" : "directory") << it->fullAbsolutePath();
		_observer->onCurrentFileChangedCallback(it->fullName());

		QFileInfo sourceFile(it->qFileInfo());
		if (!sourceFile.exists())
		{
			// Global response registered
			if (_globalResponses.count(hrFileDoesntExit) > 0)
			{
				if (_globalResponses[hrFileDoesntExit] == urSkipAll)
					continue;
			}

			_observer->onProcessHaltedCallback(hrFileDoesntExit, *it, CFileSystemObject(), QString());
			waitForResponse();
			if (_userResponse == urSkipThis || _userResponse == urSkipAll)
			{
				_userResponse = urNone;
				continue;
			}
			else if (_userResponse == urAbort)
			{
				_userResponse = urNone;
				finalize();
				return;
			}
			else
				assert (!"Unknown response");
			_userResponse = urNone;
		}

		NextAction nextAction;
		while ((nextAction = deleteItem(*it)) == naRetryOperation);

		switch (nextAction)
		{
		case naProceed:
		case naSkip:
			_observer->onProgressChangedCallback(int(currentItemIndex * 100 / _source.size()), currentItemIndex, _source.size(), 0, 0);
			++it;
			++currentItemIndex;
			break;
		case naRetryItem:
			continue;
		case naAbort:
			finalize();
			return;
		default:
			qDebug() << QString("Unexpected deleteItem() return value %1").arg(nextAction);
			Q_ASSERT(!"Unexpected deleteItem() return value");
			continue;
		}
	}

	finalize();
}

void COperationPerformer::finalize()
{
	_finished = true;
	_paused   = false;
	_observer->onProcessFinishedCallback();
}

// Iterates over all dirs in the source vector, and their subdirs, and so on and replaces _sources with a flat list of files. Returns a list of destination folders where each of the files must be copied to according to _dest
// Also counts the total size of all the files to monitor progress
std::vector<QDir> COperationPerformer::flattenSourcesAndCalcDest(uint64_t &totalSize)
{
	totalSize = 0;
	std::vector<CFileSystemObject> newSourceVector;
	std::vector<QDir> destinations;
	const bool destIsFileName = _source.size() == 1 && _destFileSystemObject.isFile();
	for (auto& o: _source)
	{
		if (o.isFile())
		{
			totalSize += o.size();
			// Ignoring the new file name here if it was supplied. We're only calculating dest dir here, not the file name
			destinations.emplace_back(destinationFolder(o.fullAbsolutePath(), o.parentDirPath(), destIsFileName ? _destFileSystemObject.parentDirPath() : _destFileSystemObject.fullAbsolutePath(), false));
			newSourceVector.push_back(o);
		}
		else if (o.isDir())
		{
			auto children = recurseDirectoryItems(o.fullAbsolutePath(), true);
			for (auto& file : children)
			{
				totalSize += file.size();
				destinations.emplace_back(destinationFolder(file.fullAbsolutePath(), o.parentDirPath(), _destFileSystemObject.fullAbsolutePath(), file.isDir()));
				newSourceVector.push_back(file);
			}
			destinations.emplace_back(destinationFolder(o.fullAbsolutePath(), o.parentDirPath(), _destFileSystemObject.fullAbsolutePath(), true));
			newSourceVector.push_back(o);
		}
	};

	_source = newSourceVector;
	return destinations;
}

UserResponse COperationPerformer::getUserResponse(HaltReason hr, const CFileSystemObject& src, const CFileSystemObject& dst, const QString& message)
{
	auto globalResponse = _globalResponses.find(hr);
	if (globalResponse != _globalResponses.end())
		return globalResponse->second;

	_observer->onProcessHaltedCallback(hr, src, dst, message);
	waitForResponse();
	const auto response = _userResponse;
	_userResponse = urNone;
	return response;
}

COperationPerformer::NextAction COperationPerformer::deleteItem(CFileSystemObject& item)
{
	if (item.isFile())
	{
		if (!item.isWriteable())
		{
			const auto response = getUserResponse(hrSourceFileIsReadOnly, item, CFileSystemObject(), item.lastErrorMessage());
			if (response == urSkipThis || response == urSkipAll)
				return naSkip;
			else if (response == urAbort)
				return naAbort;
			else if (response == urRetry)
				return naRetryOperation;
			else
				assert((response == urProceedWithThis || response == urProceedWithAll) && _newName.isEmpty());

			NextAction nextAction;
			while ((nextAction = makeItemWriteable(item)) == naRetryOperation);
			if (nextAction != naProceed)
				return nextAction;
		}
	}

	if (item.remove() != rcOk)
	{
		qDebug() << "Error removing" << (item.isFile() ? "file" : "folder") << item.fullAbsolutePath() << ", error: " << item.lastErrorMessage();
		const auto response = getUserResponse(hrFailedToDelete, item, CFileSystemObject(), item.lastErrorMessage());
		if (response == urSkipThis || response == urSkipAll)
			return naSkip;
		else if (response == urAbort)
			return naAbort;
		else if (response == urRetry)
			return naRetryOperation;
		else
		{
			Q_ASSERT(!"Unexpected user response");
			return naRetryOperation;
		}
	}

	return naProceed;
}

COperationPerformer::NextAction COperationPerformer::makeItemWriteable(CFileSystemObject& item)
{
	if (!item.makeWritable())
	{
		qDebug() << "Error making file" << item.fullAbsolutePath() << "writable, retrying";
		const auto response = getUserResponse(hrFailedToMakeItemWritable, item, CFileSystemObject(), item.lastErrorMessage());
		if (response == urSkipThis || response == urSkipAll)
			return naSkip;
		else if (response == urAbort)
			return naAbort;
		else if (response == urRetry)
			return naRetryOperation;
		else
		{
			Q_ASSERT(!"Unexpected user response");
			return naRetryOperation;
		}
	}

	return naProceed;
}

QDir destinationFolder(const QString &absoluteSourcePath, const QString &originPath, const QString &destPath, bool /*sourceIsDir*/)
{
	QString localPath = absoluteSourcePath.mid(originPath.length());
	assert(!localPath.isEmpty());
	if (localPath.startsWith('\\') || localPath.startsWith('/'))
		localPath.remove(0, 1);

	const QString tmp = QFileInfo(destPath).absoluteFilePath();
	assert(QString(destPath % "/").remove("//") == QString(tmp % "/").remove("//"));
	const QString result = destPath % "/" % localPath;
	return QFileInfo(result).absolutePath();
}
