#include "coperationperformer.h"
#include "cfilemanipulator.h"
#include "directoryscanner.h"

#include "assert/advanced_assert.h"
#include "threading/thread_helpers.h"
#include "utility/on_scope_exit.hpp"
#include "utility/integer_literals.hpp"

#include "3rdparty/magic_enum/magic_enum.hpp"
#include "lang/utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

inline HaltReason haltReasonForOperationError(FileOperationResultCode errorCode)
{
	assert_without_abort(errorCode != FileOperationResultCode::Ok);

	switch (errorCode)
	{
	case FileOperationResultCode::ObjectDoesntExist:
		return hrFileDoesntExit;
	case FileOperationResultCode::TargetAlreadyExists:
		return hrFileExists;
	case FileOperationResultCode::NotEnoughSpaceAvailable:
		return hrNotEnoughSpace;
	default:
		return hrUnknownError;
	}
}

COperationPerformer::COperationPerformer(const Operation operation, std::vector<CFileSystemObject>&& source, QString destination) :
	_destFileSystemObject(destination),
	_op(operation)
{
	_source.reserve(source.size());
	for (auto&& o: std::move(source))
		_source.emplace_back(std::move(o));
}

COperationPerformer::COperationPerformer(const Operation operation, const CFileSystemObject& source, QString destination /*= QString()*/) :
	COperationPerformer(operation, std::vector<CFileSystemObject>{source}, destination)
{
}

COperationPerformer::~COperationPerformer()
{
	cancel();

	if (_thread.joinable())
		_thread.join();
}

void COperationPerformer::setObserver(CFileOperationObserver *observer)
{
	assert_r(observer);
	_observer = observer;
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
	return _done;
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
}

void COperationPerformer::cancel()
{
	_cancelRequested = true;
}

void COperationPerformer::threadFunc()
{
	_inProgress = true;

	EXEC_ON_SCOPE_EXIT([this]() {finalize();});

	setThreadName("COperationPerformer thread");

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
		assert_and_return_r("Uknown operation", );
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

QDebug& operator<<(QDebug& stream, const std::vector<COperationPerformer::ObjectToProcess>& objects)
{
	for (const auto& object : objects)
		stream << object.object.fullAbsolutePath() << '\n';
	return stream;
}

void COperationPerformer::copyFiles()
{
	if (_source.empty())
		return;

	assert_and_return_r(_op == operationCopy || _op == operationMove, );
	qInfo() << __FUNCTION__ << (_op == operationCopy ? "Copying directory" : "Moving directory") << _source << "to" << _destFileSystemObject.fullAbsolutePath();

	size_t currentItemIndex = 0;

	// If there's just one file to copy, it is allowed to set a new file name as dest (C:/1.txt) instead of just the path (C:/)
	if (_source.size() == 1 && _source.front().object.isFile() && !_destFileSystemObject.isDir())
		_newName = _destFileSystemObject.fullName();

	// Check if source and dest are on the same file system / disk drive, in which case moving is much simpler and faster.
	// Moving means renaming the root source folder / file, which is fast and simple. Just make sure the destination folder exists.
	if (_op == operationMove && _source.front().object.isMovableTo(_destFileSystemObject))
	{
		assert_r(std::all_of(_source.cbegin() + 1, _source.cend(), [this](const ObjectToProcess& o) { return o.object.isMovableTo(_destFileSystemObject); }));
		_totalTimeElapsed.start();

		// TODO: Assuming that all sources are from the same drive / file system. Can that assumption ever be incorrect?
		for (auto sourceIterator = _source.begin(); sourceIterator != _source.end() && !_cancelRequested; _userResponse = urNone /* needed for normal operation of condition variable */)
		{
			if (sourceIterator->object.isCdUp())
			{
				++sourceIterator;
				++currentItemIndex;
				continue;
			}

			const QString newFileName = !_newName.isEmpty() ? _newName :  sourceIterator->object.fullName();
			_newName.clear();
			CFileManipulator itemManipulator(sourceIterator->object);
			const auto result = itemManipulator.moveAtomically(_destFileSystemObject.isDir() ? _destFileSystemObject.fullAbsolutePath() : _destFileSystemObject.parentDirPath(), newFileName);
			if (result != FileOperationResultCode::Ok)
			{
				const auto response = getUserResponse(result == FileOperationResultCode::TargetAlreadyExists ? hrFileExists : hrUnknownError, sourceIterator->object, CFileSystemObject(_destFileSystemObject.fullAbsolutePath() % '/' % newFileName), itemManipulator.lastErrorMessage());
				// Handler is identical to that of the main loop
				// esp. for the case of hrFileExists
				if (response == urSkipThis || response == urSkipAll)
				{
					++sourceIterator;
					++currentItemIndex;
					continue;
				}
				else if (response == urAbort)
					return;
				else if (response == urRename)
					// _newName has been set and will be taken into account
					continue;
				else if (response == urRetry)
					continue;
				else
					assert_r(response == urProceedWithThis || response == urProceedWithAll);
			}

			++sourceIterator;
			++currentItemIndex;
		}

		return;
	}

	uint64_t totalSize = 0;
	uint64_t sizeProcessed = 0;

	const auto destination = enumerateSourcesAndCalcDest(totalSize);
	qInfo() << __FUNCTION__ << "total size:" << totalSize << "bytes";
	assert_r(destination.size() == _source.size());

	std::vector<CFileSystemObject> dirsToCleanUp;

	_totalTimeElapsed.start();

	for (auto sourceIterator = _source.begin(); sourceIterator != _source.end() && !_cancelRequested; _userResponse = urNone /* needed for normal operation of condition variable */)
	{
		if (sourceIterator->object.isCdUp())
		{
			++sourceIterator;
			++currentItemIndex;
			continue;
		}

#if defined _DEBUG
		qInfo() << __FUNCTION__ << "Processing" << (sourceIterator->object.isFile() ? "file" : "DIR ") << sourceIterator->object.fullAbsolutePath();
#endif
		if (_observer) _observer->onCurrentFileChangedCallback(sourceIterator->object.fullName());

		const QFileInfo& sourceFileInfo = sourceIterator->object.qFileInfo();
		if (!sourceFileInfo.exists())
		{
			const auto response = getUserResponse(hrFileDoesntExit, sourceIterator->object, CFileSystemObject(), QString());
			if (response == urSkipThis || response == urSkipAll)
			{
				++sourceIterator;
				++currentItemIndex;
				continue;
			}
			else if (response == urAbort)
				return;
			else
				assert_unconditional_r("Unknown response");
		}

		QFileInfo destInfo(destination[currentItemIndex].absoluteFilePath(_newName.isEmpty() ? sourceIterator->object.fullName() : _newName));
		_newName.clear();
		if (destInfo.absoluteFilePath() == sourceFileInfo.absoluteFilePath())
		{
			++sourceIterator;
			++currentItemIndex;
			continue;
		}

		if (sourceIterator->object.isFile())
		{
			NextAction nextAction;
			while ((nextAction = copyItem(sourceIterator->object, destInfo, destination[currentItemIndex], sizeProcessed, totalSize, currentItemIndex)) == naRetryOperation);
			switch (nextAction)
			{
			case naProceed:
				sourceIterator->copiedSuccessfully = true;
				break;
			case naSkip:
				sizeProcessed += sourceIterator->object.size();
				++sourceIterator;
				++currentItemIndex;
				continue;
			case naRetryItem:
				continue;
			case naRetryOperation:
			case naAbort:
				return;
			default:
				assert_unconditional_r(QString("Unexpected deleteItem() return value %1").arg(nextAction).toUtf8().constData());
				continue; // Retry
			}

			if (_op == operationMove) // result == ok
			{
				if (!sourceIterator->copiedSuccessfully) // A fix for #270 (Cancelling a move operation deletes the original file nonetheless)
				{
					++sourceIterator;
					++currentItemIndex;
					continue;
				}

				while ((nextAction = deleteItem(sourceIterator->object)) == naRetryOperation);

				switch (nextAction)
				{
				case naProceed:
					break;
				case naSkip:
					++sourceIterator;
					++currentItemIndex;
					continue;
				case naRetryItem:
					continue;
				case naAbort:
					return;
				default:
					assert_unconditional_r("Unexpected deleteItem() return value " + std::to_string(nextAction));
					continue; // Retry
				}
			}
		}
		else if (sourceIterator->object.isDir())
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
					++sourceIterator;
					continue;
				}
				else if (nextAction == naRetryOperation)
					return;
				else if (nextAction != naProceed)
					assert_unconditional_r("Unexpected next action");
				else
					sourceIterator->copiedSuccessfully = true;
			}

			if (_op == operationMove)
			{
				if (!sourceIterator->copiedSuccessfully) // A fix for #270 (Cancelling a move operation deletes the original file nonetheless)
				{
					++sourceIterator;
					++currentItemIndex;
					continue;
				}

				if (sourceIterator->object.isEmptyDir())
				{
					CFileManipulator manipulator(sourceIterator->object);
					const auto result = manipulator.remove();
					if (result != FileOperationResultCode::Ok)
					{
						const auto action = getUserResponse(hrFailedToDelete, sourceIterator->object, CFileSystemObject(), manipulator.lastErrorMessage());
						if (action == urSkipThis || action == urSkipAll)
						{
							++sourceIterator;
							++currentItemIndex;
							continue;
						}
						else if (action == urAbort)
							return;
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
					dirsToCleanUp.emplace_back(sourceIterator->object);
			}
		}

		if (sourceIterator->object.isFile())
			sizeProcessed += sourceIterator->object.size();

		++sourceIterator;
		++currentItemIndex;
	}

	for (auto& dir : dirsToCleanUp)
	{
		CFileManipulator dirManipulator(dir);
		assert_message_r(dirManipulator.remove() == FileOperationResultCode::Ok, dirManipulator.lastErrorMessage().toUtf8().constData());
	}

	qInfo() << __FUNCTION__ << "took" << _totalTimeElapsed.elapsed() << "ms";
}

void COperationPerformer::deleteFiles()
{
	std::vector<CFileSystemObject> fileSystemObjectsList;
	fileSystemObjectsList.reserve(500);

	for (auto it = _source.begin(); it != _source.end() && !_cancelRequested; ++it, _userResponse = urNone /* needed for normal condition variable operation */)
	{
		if (!it->object.isCdUp())
		{
			scanDirectory(it->object, [&fileSystemObjectsList](const CFileSystemObject& item) {
				fileSystemObjectsList.emplace_back(item);
			});
		}
	}

	_totalTimeElapsed.start();

	const size_t totalNumberOfObjects = fileSystemObjectsList.size();
	size_t currentItemIndex = 0;
	for (auto it = fileSystemObjectsList.begin(); it != fileSystemObjectsList.end() && !_cancelRequested; _userResponse = urNone /* needed for normal condition variable operation */)
	{
		handlePause();

		if (!it->isFile())
		{
			++it;
			continue;
		}

#if defined _DEBUG
		qInfo() << __FUNCTION__ << "deleting file" << it->fullAbsolutePath();
#endif
		if (_observer) _observer->onCurrentFileChangedCallback(it->fullName());

		const uint64_t speed = (currentItemIndex + 1) * 1000000 / std::max(_totalTimeElapsed.elapsed<std::chrono::microseconds>(), (uint64_t)1);
		const uint32_t secondsRemaining = speed > 0 ? (uint32_t) ((totalNumberOfObjects - currentItemIndex - 1) / speed) : 0;
		if (_observer) _observer->onProgressChangedCallback(currentItemIndex * 100.0f / totalNumberOfObjects, currentItemIndex, totalNumberOfObjects, 0, speed, secondsRemaining);

		if (!it->exists())
		{
			const auto response = getUserResponse(hrFileDoesntExit, *it, CFileSystemObject(), QString());
			if (response == urSkipThis || response == urSkipAll)
			{
				++it;
				++currentItemIndex;
				continue;
			}
			else if (response == urRetry)
				continue;
			else if (response == urAbort)
				return;
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
			++it;
			++currentItemIndex;
			break;
		case naRetryItem:
			continue;
		case naAbort:
			return;
		default:
			assert_unconditional_r("Unexpected deleteItem() return value " + std::to_string(nextAction));
			continue;
		}
	}

	// TODO: eliminate code duplication
	// We know that files and directories are being enumerated depth-first, so we need to delete them in reverse order to avoid trying to delete non-empty directories
	for (auto it = fileSystemObjectsList.rbegin(); it != fileSystemObjectsList.rend() && !_cancelRequested; _userResponse = urNone /* needed for normal condition variable operation */)
	{
		handlePause();

		if (!it->isDir())
		{
			++it;
			continue;
		}

		if (_observer) _observer->onCurrentFileChangedCallback(it->fullName());

		const uint64_t speed = (currentItemIndex + 1) * 1000000 / std::max(_totalTimeElapsed.elapsed<std::chrono::microseconds>(), (uint64_t)1);
		const uint32_t secondsRemaining = speed > 0 ? (uint32_t) ((totalNumberOfObjects - currentItemIndex) / speed) : 0;
		if (_observer) _observer->onProgressChangedCallback(currentItemIndex * 100.0f / totalNumberOfObjects, currentItemIndex, totalNumberOfObjects, 0, speed, secondsRemaining);

		if (!it->exists())
		{
			const auto response = getUserResponse(hrFileDoesntExit, *it, CFileSystemObject(), QString());
			if (response == urSkipThis || response == urSkipAll)
			{
				++it;
				++currentItemIndex;
				continue;
			}
			else if (response == urRetry)
				continue;
			else if (response == urAbort)
				return;
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
			++it;
			++currentItemIndex;
			break;
		case naRetryItem:
			continue;
		case naAbort:
			return;
		default:
			assert_unconditional_r(QString("Unexpected deleteItem() return value %1").arg(nextAction).toUtf8().constData());
			continue;
		}
	}
}

void COperationPerformer::finalize()
{
	_done = true;
	_paused   = false;
	if (_observer) _observer->onProcessFinishedCallback();
}

inline bool isAbsolutePath(const QString& path)
{
#ifdef _WIN32
	return path.contains(':') || path.startsWith("//");
#else
	return path.startsWith('/');
#endif
}

inline QDir destinationFolder(const QString &absoluteSourcePath, const QString &originPath, const QString &destPath, bool /*sourceIsDir*/)
{
	QString localPath = absoluteSourcePath.mid(originPath.length());
	assert_r(!localPath.isEmpty());
	if (localPath.startsWith('\\') || localPath.startsWith('/'))
		localPath = localPath.remove(0, 1);

	assert_debug_only(isAbsolutePath(destPath));
	return CFileSystemObject(destPath % '/' % localPath).parentDirPath();
}

// Iterates over all dirs in the source vector, and their subdirs, and so on and replaces _sources with a flat list of files. Returns a list of destination folders where each of the files must be copied to according to _dest
// Also counts the total size of all the files to monitor progress

// TODO: refactor to a separate algorithm that iterates recursively over subdirs.
// Then I would no longer need to calculate the total size of all files in the same method just to avoid code duplication.
std::vector<QDir> COperationPerformer::enumerateSourcesAndCalcDest(uint64_t &totalSize)
{
	totalSize = 0;
	std::vector<ObjectToProcess> newSourceVector;
	std::vector<QDir> destinations;
	const bool destIsFileName = _source.size() == 1 && !_destFileSystemObject.isDir();
	for (auto& o: _source)
	{
		if (o.object.isFile())
		{
			totalSize += o.object.size();
			// Ignoring the new file name here if it was supplied. We're only calculating dest dir here, not the file name
			destinations.emplace_back(destinationFolder(o.object.fullAbsolutePath(), o.object.parentDirPath(), destIsFileName ? _destFileSystemObject.parentDirPath() : _destFileSystemObject.fullAbsolutePath(), false));
			newSourceVector.emplace_back(o.object);
		}
		else if (o.object.isDir())
		{
			scanDirectory(o.object, [&destinations, &newSourceVector, &totalSize, &o, this](const CFileSystemObject& item) {
				destinations.emplace_back(destinationFolder(item.fullAbsolutePath(), o.object.parentDirPath(), _destFileSystemObject.fullAbsolutePath(), item.isDir() /* TODO: 'false' ? */));

				if (item.isFile())
					totalSize += item.size();

				newSourceVector.emplace_back(std::move(item));
			});
		}
	}

	_source = std::move(newSourceVector);
	return destinations;
}

UserResponse COperationPerformer::getUserResponse(HaltReason hr, const CFileSystemObject& src, const CFileSystemObject& dst, const QString& message)
{
	auto globalResponse = _globalResponses.find(hr);
	if (globalResponse != _globalResponses.end())
		return globalResponse->second;

	if (_observer) _observer->onProcessHaltedCallback(hr, src, dst, message);
	waitForResponse();
	const auto response = _userResponse;
	_userResponse = urNone;
	return response;
}

COperationPerformer::NextAction COperationPerformer::deleteItem(CFileSystemObject& item)
{
	CFileManipulator itemManipulator(item);
	if (item.isFile() && !item.isWriteable())
	{
		const auto response = getUserResponse(hrSourceFileIsReadOnly, item, CFileSystemObject(), itemManipulator.lastErrorMessage()); // TODO: is the message "itemManipulator.lastErrorMessage()" correct here? No operation had been attempted yet
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

	if (itemManipulator.remove() != FileOperationResultCode::Ok)
	{
		assert_unconditional_r((QString("Error removing ") + (item.isFile() ? "file " : "folder ") + item.fullAbsolutePath() + ", error: " + itemManipulator.lastErrorMessage()).toStdString());
		const auto response = getUserResponse(hrFailedToDelete, item, CFileSystemObject(), itemManipulator.lastErrorMessage());
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
	CFileManipulator itemManipulator(item);
	if (!itemManipulator.makeWritable())
	{
		assert_unconditional_r((QString("Error making file ") + item.fullAbsolutePath() + " writable, retrying").toStdString());
		const auto response = getUserResponse(hrFailedToMakeItemWritable, item, CFileSystemObject(), itemManipulator.lastErrorMessage());
		if (response == urSkipThis || response == urSkipAll)
			return naSkip;
		else if (response == urAbort)
			return naAbort;
		else if (response == urRetry)
			return naRetryOperation;
		else
			assert_and_return_unconditional_r("Unexpected user response", naRetryOperation);
	}

	return naProceed;
}

COperationPerformer::NextAction COperationPerformer::copyItem(CFileSystemObject& item, const QFileInfo& destInfo, const QDir& destDir, uint64_t sizeProcessedPreviously, uint64_t totalSize, size_t currentItemIndex)
{
	if (!item.isFile())
		return naProceed;

	CFileSystemObject destFile(destInfo);

	if (destFile.exists() && destFile.isFile())
	{
		auto response = getUserResponse(hrFileExists, item, destFile, QString());
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
			response = getUserResponse(hrDestFileIsReadOnly, destFile, CFileSystemObject(), QString());
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

#ifndef OPERATION_PERFORMER_CHUNK_SIZE
	static constexpr uint64_t chunkSize = 5ULL * 1024ULL * 1024ULL;
#else
	static constexpr uint64_t chunkSize = OPERATION_PERFORMER_CHUNK_SIZE;
#endif
	const QString destPath = destDir.absolutePath() + '/';
	auto result = FileOperationResultCode::Fail;
	CFileManipulator itemManipulator(item);

	do
	{
		handlePause();

		result = itemManipulator.copyChunk(chunkSize, destPath, _newName.isEmpty() ? (!destFile.isDir() ? destFile.fullName() : QString()) : _newName);
		// Error handling
		if (result != FileOperationResultCode::Ok)
			break;

		if (_observer)
		{
			const uint64_t bytesProcessed /* bytes */ = sizeProcessedPreviously + itemManipulator.bytesCopied();
			const float totalPercentage = totalSize > 0 ? (bytesProcessed * 100) / static_cast<float>(totalSize) : 0.0f;
			const float filePercentage = item.size() > 0 ? (itemManipulator.bytesCopied() * 100) / static_cast<float>(item.size()) : 0.0f;
			// Bytes / sec
			const uint64_t meanSpeed = bytesProcessed > 0 ? bytesProcessed * 1'000'000 / std::max(_totalTimeElapsed.elapsed<std::chrono::microseconds>(), 1_u64) : 0;
			const uint64_t bytesRemaining = totalSize - bytesProcessed;
			const uint32_t secondsRemaining = meanSpeed > 0 ? static_cast<uint32_t>(bytesRemaining / meanSpeed) : 0;

			_observer->onProgressChangedCallback(totalPercentage, currentItemIndex, _source.size(), filePercentage, meanSpeed, secondsRemaining);
		}

		// TODO: why isn't this block at the start of 'do-while'?
		if (_cancelRequested)
		{
			assert_message_r(itemManipulator.cancelCopy() == FileOperationResultCode::Ok, "Failed to cancel item copying");
			result = FileOperationResultCode::Ok;
			break;
		}
	} while (itemManipulator.copyOperationInProgress());


	if (result != FileOperationResultCode::Ok)
	{
		assert_r(itemManipulator.cancelCopy() == FileOperationResultCode::Ok);

		QString actualNewName = _newName;
		if (actualNewName.isEmpty() && destInfo.isFile())
			actualNewName = destInfo.fileName();

		const QString errorMessage =
			"Error copying file " % item.fullAbsolutePath() %
			" to " % destPath %
			actualNewName %
			", error: " % itemManipulator.lastErrorMessage();

		assert_unconditional_r(errorMessage.toStdString());

		const auto action = getUserResponse(haltReasonForOperationError(result), item, CFileSystemObject(), itemManipulator.lastErrorMessage());
		if (action == urSkipThis || action == urSkipAll)
			return naSkip;
		else if (action == urAbort)
			return naAbort;
		else if (action == urRetry)
			return naRetryOperation;
		else
			assert_and_return_unconditional_r("Unexpected user response", naRetryOperation);
	}

	return naProceed;
}

COperationPerformer::NextAction COperationPerformer::mkPath(const QDir& dir)
{
	if (dir.mkpath(".") || dir.exists())
		return naProceed;

	const auto response = getUserResponse(hrCreatingFolderFailed, CFileSystemObject(dir), CFileSystemObject(), QString());
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

void COperationPerformer::handlePause()
{
	if (_paused) // This code is not strictly thread-safe (the value of _paused may change between 'if' and 'while'), but in this context I'm OK with that
	{
		_totalTimeElapsed.pause();
		while (_paused)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

		_totalTimeElapsed.resume();
	}
}

void CFileOperationObserver::processEvents()
{
	std::lock_guard<std::mutex> lock(_callbackMutex);
	for (const auto& event : _callbacks)
		event();

	_callbacks.clear();
}

void CFileOperationObserver::onProgressChangedCallback(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed, uint32_t secondsRemaining)
{
	assert_debug_only(filePercentage < 100.5f && totalPercentage < 100.5f);
	std::lock_guard<std::mutex> lock(_callbackMutex);
	_callbacks.emplace_back([=, this]() {
		onProgressChanged(totalPercentage, numFilesProcessed, totalNumFiles, filePercentage, speed, secondsRemaining);
	});
}

void CFileOperationObserver::onProcessHaltedCallback(HaltReason reason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage)
{
	const auto reasonString = magic_enum::enum_name(reason);
	qInfo().nospace() << "COperationPerformer: process halted, reason: " << QString::fromLatin1(reasonString.data(), (int)reasonString.size()) << ", source: " << source.fullAbsolutePath() << ", dest: " << dest.fullAbsolutePath() << ", error message: " << errorMessage;

	std::lock_guard<std::mutex> lock(_callbackMutex);
	_callbacks.emplace_back([reason, mv(source), mv(dest), mv(errorMessage), this]() {
		onProcessHalted(reason, source, dest, errorMessage);
	});
}

void CFileOperationObserver::onProcessFinishedCallback(QString message)
{
	if (!message.isEmpty())
		qInfo() << "COperationPerformer: operation finished, message:" << message;

	std::lock_guard<std::mutex> lock(_callbackMutex);
	_callbacks.emplace_back([mv(message), this]() {
		onProcessFinished(message);
	});
}

void CFileOperationObserver::onCurrentFileChangedCallback(QString file)
{
	std::lock_guard<std::mutex> lock(_callbackMutex);
	_callbacks.emplace_back([mv(file), this]() {
		onCurrentFileChanged(file);
	});
}
