#include "coperationperformer.h"
#include "filesystemhelperfunctions.h"

#include <functional>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

inline QDir destinationFolder(const QString &absoluteSourcePath, const QString &originPath, const QString &destPath, bool /*sourceIsDir*/)
{
	QString localPath = absoluteSourcePath.mid(originPath.length());
	assert_r(!localPath.isEmpty());
	if (localPath.startsWith('\\') || localPath.startsWith('/'))
		localPath = localPath.remove(0, 1);

	const QString tmp = QFileInfo(destPath).absoluteFilePath();
	assert_r(QString(destPath % "/").remove("//") == QString(tmp % "/").remove("//"));
	const QString result = destPath % "/" % localPath;
	return QFileInfo(result).absolutePath();
}

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

	if (_thread.joinable())
		_thread.join();
}

void COperationPerformer::setWatcher(CFileOperationObserver *watcher)
{
	assert_r(watcher);
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
	assert_r(_userResponse == urNone); // _userResponse should have been reset after being used
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
		assert_unconditional_r("Uknown operation");
		return;
	}
}

void COperationPerformer::waitForResponse()
{
	std::unique_lock<std::mutex> lock(_waitForResponseMutex);
	_totalTimeElapsed.pause();
	while (_userResponse == urNone)
		_waitForResponseCondition.wait(lock);

	_totalTimeElapsed.resume();
}

void COperationPerformer::copyFiles()
{
	CTimeElapsed timer;
	timer.start();

	if (_source.empty())
	{
		finalize();
		return;
	}

	assert_r(_op == operationCopy || _op == operationMove);

	_inProgress = true;

	_totalTimeElapsed.start();
	size_t currentItemIndex = 0;

	if (_source.size() == 1)
		// If there's just one file to copy, it is allowed to set a new file name as dest (C:/1.txt) instead of just the path (C:/)
		// Or we're just renaming an item, no matter file or dir, in which case we also must account for the new name
		if ((_source.front().isFile() && !_destFileSystemObject.isDir()) || _source.front().isDir())
			_newName = _destFileSystemObject.fullName();

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

			const QString newFileName = !_newName.isEmpty() ? _newName :  it->fullName();
			_newName.clear();
			const auto result = it->moveAtomically(_destFileSystemObject.isDir() ? _destFileSystemObject.fullAbsolutePath() : _destFileSystemObject.parentDirPath(), newFileName);
			if (result != rcOk)
			{
				const auto response = getUserResponse(result == rcTargetAlreadyExists ? hrFileExists : hrUnknownError, *it, CFileSystemObject(_destFileSystemObject.fullAbsolutePath() % '/' % newFileName), it->lastErrorMessage());
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
					// _newName has been set and will be taken into account
					continue;
				else if (response == urRetry)
					continue;
				else
					assert_r(response == urProceedWithThis || response == urProceedWithAll);
			}

			++it;
			++currentItemIndex;
		}

		finalize();
		return;
	}

	uint64_t totalSize = 0, sizeProcessed = 0;
	const auto destination = flattenSourcesAndCalcDest(totalSize);
	assert_r(destination.size() == _source.size());

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
			const auto response = getUserResponse(hrFileDoesntExit, *it, CFileSystemObject(), QString::null);
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
			else
				assert_unconditional_r("Unknown response");
		}

		QFileInfo destInfo(destination[currentItemIndex].absoluteFilePath(_newName.isEmpty() ? it->fullName() : _newName));
		_newName.clear();
		if (destInfo.absoluteFilePath() == sourceFileInfo.absoluteFilePath())
		{
			++it;
			++currentItemIndex;
			continue;
		}

		if (it->isFile())
		{
			NextAction nextAction;
			while ((nextAction = copyItem(*it, destInfo, destination[currentItemIndex], sizeProcessed, totalSize, currentItemIndex)) == naRetryOperation);
			switch (nextAction)
			{
			case naProceed:
				break;
			case naSkip:
				++it;
				++currentItemIndex;
				sizeProcessed += it->size();
				continue;
			case naRetryItem:
				continue;
			case naRetryOperation:
			case naAbort:
				finalize();
				return;
			default:
				assert_unconditional_r(QString("Unexpected deleteItem() return value %1").arg(nextAction).toUtf8().constData());
				continue; // Retry
			}

			if (_op == operationMove) // result == ok
			{
				while ((nextAction = deleteItem(*it)) == naRetryOperation);

				switch (nextAction)
				{
				case naProceed:
					break;
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
					assert_unconditional_r("Unexpected deleteItem() return value");
					continue; // Retry
				}
			}
		}
		else if (it->isDir())
		{
			// Creating the folder - empty folders will not be copied without this code
			CFileSystemObject destObject(destInfo);
			if (!destObject.exists())
			{
				NextAction nextAction;
				while ((nextAction = mkPath(QDir(destObject.fullAbsolutePath()))) == naRetryOperation);
				if (nextAction == naRetryItem)
					continue;
				else if (nextAction == naSkip)
				{
					++currentItemIndex;
					++it;
					continue;
				}
				else if (nextAction == naRetryOperation)
				{
					finalize();
					return;
				}
				else if (nextAction != naProceed)
					assert_unconditional_r("Unexpected next action");
			}

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
							continue;
						else
						{
							assert_unconditional_r("Unexpected next action");
							continue; // Retry
						}
					}
				}
				else // not empty
					dirsToCleanUp.push_back(*it);
			}
		}

		sizeProcessed += it->size();

		++it;
		++currentItemIndex;
	}

	for (auto& dir: dirsToCleanUp)
		dir.remove();

	qDebug() << __FUNCTION__ << "took" << timer.elapsed() << "ms";
	finalize();
}

void COperationPerformer::deleteFiles()
{
	_inProgress = true;

	_totalTimeElapsed.start();

	size_t currentItemIndex = 0;
	std::vector<DirectoryHierarchy> hierarchy;
	for (auto it = _source.begin(); it != _source.end() && !_cancelRequested; ++it, _userResponse = urNone /* needed for normal condition variable operation */)
	{
		if (!it->isCdUp())
			hierarchy.emplace_back(enumerateDirectoryRecursively(*it));
	}

	auto fileSystemObjectsList = flattenHierarchy(hierarchy);

	const size_t totalNumberOfObjects = fileSystemObjectsList.directories.size() + fileSystemObjectsList.files.size();
	for (auto it = fileSystemObjectsList.files.begin(); it != fileSystemObjectsList.files.end() && !_cancelRequested; _userResponse = urNone /* needed for normal condition variable operation */)
	{
		qDebug() << __FUNCTION__ << "deleting file" << it->fullAbsolutePath();
		_observer->onCurrentFileChangedCallback(it->fullName());

		if (!it->exists())
		{
			const auto response = getUserResponse(hrFileDoesntExit, *it, CFileSystemObject(), QString::null);
			if (response == urSkipThis || response == urSkipAll)
			{
				++it;
				++currentItemIndex;
				continue;
			}
			else if (response == urRetry)
				continue;
			else if (response == urAbort)
			{
				finalize();
				return;
			}
			else
				assert_unconditional_r("Unknown response");
		}

		NextAction nextAction;
		while ((nextAction = deleteItem(*it)) == naRetryOperation);

		switch (nextAction)
		{
		case naProceed:
			++it;
			++currentItemIndex;
			break;
		case naSkip:
			_observer->onProgressChangedCallback(currentItemIndex * 100.0f / totalNumberOfObjects, currentItemIndex, totalNumberOfObjects, 0, 0);
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
			assert_unconditional_r("Unexpected deleteItem() return value");
			continue;
		}
	}

	// TODO: eliminate code duplication
	// We know that files and directories are being enumerated depth-first, so we need to delete them in reverse order to avoid trying to delete non-empty directories
	for (auto it = fileSystemObjectsList.directories.rbegin(); it != fileSystemObjectsList.directories.rend() && !_cancelRequested; _userResponse = urNone /* needed for normal condition variable operation */)
	{
		qDebug() << __FUNCTION__ << "deleting directory" << it->fullAbsolutePath();
		_observer->onCurrentFileChangedCallback(it->fullName());

		if (!it->exists())
		{
			const auto response = getUserResponse(hrFileDoesntExit, *it, CFileSystemObject(), QString::null);
			if (response == urSkipThis || response == urSkipAll)
			{
				++it;
				++currentItemIndex;
				continue;
			}
			else if (response == urRetry)
				continue;
			else if (response == urAbort)
			{
				finalize();
				return;
			}
			else
				assert_unconditional_r("Unknown response");
		}

		NextAction nextAction;
		while ((nextAction = deleteItem(*it)) == naRetryOperation);

		switch (nextAction)
		{
		case naProceed:
			++it;
			++currentItemIndex;
			break;
		case naSkip:
			_observer->onProgressChangedCallback(currentItemIndex * 100.0f / totalNumberOfObjects, currentItemIndex, totalNumberOfObjects, 0, 0);
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
			assert_unconditional_r("Unexpected deleteItem() return value");
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
	const bool destIsFileName = _source.size() == 1 && !_destFileSystemObject.isDir();
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
			const auto children = flattenHierarchy(enumerateDirectoryRecursively(o));
			for (const auto& file : children.files)
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
				assert_r((response == urProceedWithThis || response == urProceedWithAll) && _newName.isEmpty());

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
			assert_unconditional_r("Unexpected user response");
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
			assert_unconditional_r("Unexpected user response");
			return naRetryOperation;
		}
	}

	return naProceed;
}

COperationPerformer::NextAction COperationPerformer::copyItem(CFileSystemObject& item, const QFileInfo& destInfo, const QDir& destDir, uint64_t sizeProcessed, uint64_t totalSize, size_t currentItemIndex)
{
	if (!item.isFile())
		return naProceed;

	CFileSystemObject destFile(destInfo);

	if (destFile.exists() && destFile.isFile())
	{
		auto response = getUserResponse(hrFileExists, item, destFile, QString::null);
		if (response == urSkipThis || response == urSkipAll)
			return naSkip;
		else if (response == urAbort)
			return naAbort;
		else if (response == urRetry)
			return naRetryItem;
		else if (response == urRename)
		{
			assert_r(!_newName.isEmpty());
			// Continue - the new name will be accounted for
		}
		else if (response != urProceedWithThis && response != urProceedWithAll)
		{
			assert_unconditional_r("Unexpected user response");
			return naRetryItem;
		}

		// Only call isWriteable for existing items!
		if (!destFile.isWriteable())
		{
			auto response = getUserResponse(hrDestFileIsReadOnly, destFile, CFileSystemObject(), QString::null);
			if (response == urSkipThis || response == urSkipAll)
				return naSkip;
			else if (response == urAbort)
				return naAbort;
			else if (response == urRetry)
				return naRetryOperation;
			else
				assert_r((response == urProceedWithThis || response == urProceedWithAll) && _newName.isEmpty());

			NextAction nextAction;
			while ((nextAction = makeItemWriteable(destFile)) == naRetryOperation);
			if (nextAction != naProceed)
				return nextAction;
		}
	}

	if (!destDir.exists())
	{
		NextAction nextAction;
		while ((nextAction = mkPath(destDir)) == naRetryOperation);
		if (nextAction != naProceed)
			return nextAction;
	}

	const int chunkSize = 5 * 1024 * 1024;
	const QString destPath = destDir.absolutePath() + '/';
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

		result = item.copyChunk(chunkSize, destPath, _newName.isEmpty() ? (!destFile.isDir() ? destFile.fullName() : QString::null) : _newName);
		// Error handling
		if (result != rcOk)
			break;

		const float totalPercentage = totalSize > 0 ? float(sizeProcessed + item.bytesCopied()) * 100.0f / totalSize : 0.0f;
		const float filePercentage = item.size() > 0 ? item.bytesCopied() * 100.0f / item.size() : 0.0f;
		const uint64_t speed = _fileTimeElapsed.elapsed() > 0 ? item.bytesCopied() * 1000 / _fileTimeElapsed.elapsed() : 0; // B/s
		_smoothSpeedCalculator = speed;
		_observer->onProgressChangedCallback(totalPercentage, currentItemIndex, _source.size(), filePercentage, _smoothSpeedCalculator.arithmeticMean());

		// TODO: why isn't this block at the start of 'do-while'?
		if (_cancelRequested)
		{
			if (item.cancelCopy() != rcOk)
				assert_unconditional_r("Failed to cancel item copying");
			result = rcOk;
			break;
		}
	} while (item.copyOperationInProgress());

	if (result != rcOk)
	{
		item.cancelCopy();
		qDebug() << "Error copying file " << item.fullAbsolutePath() << " to " << destPath + (_newName.isEmpty() ? (destInfo.isFile() ? destInfo.fileName() : QString::null) : _newName) << ", error: " << item.lastErrorMessage();
		const auto action = getUserResponse(hrUnknownError, item, CFileSystemObject(), item.lastErrorMessage());
		if (action == urSkipThis || action == urSkipAll)
			return naSkip;
		else if (action == urAbort)
			return naAbort;
		else if (action == urRetry)
			return naRetryOperation;
		else
		{
			assert_unconditional_r("Unexpected user response");
			return naRetryOperation;
		}
	}

	return naProceed;
}

COperationPerformer::NextAction COperationPerformer::mkPath(const QDir& dir)
{
	if (dir.mkpath(".") || dir.exists())
		return naProceed;

	const auto response = getUserResponse(hrCreatingFolderFailed, CFileSystemObject(dir), CFileSystemObject(), QString::null);
	if (response == urSkipThis || response == urSkipAll)
		return naSkip;
	else if (response == urAbort)
		return naAbort;
	else if (response == urRetry)
		return naRetryOperation;
	else
	{
		assert_unconditional_r("Unexpected user response");
		return naRetryItem;
	}
}
