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
	_dest(toPosixSeparators(destination)),
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
	_inProgress = true;
	uint64_t totalSize = 0, sizeProcessed = 0;
	const auto destination = flattenSourcesAndCalcDest(totalSize);
	assert (destination.size() == _source.size());
	// If there's just one file to copy it is allowed to set a new file name as dest (C:/1.txt) instead of just the path (C:/)
	QString newFileName;
	if (_source.size() == 1 && _source.front().isFile())
	{
		QFileInfo destInfo(_dest);
		newFileName = !destInfo.isDir() ? destInfo.fileName() : QString();
	}

	_totalTimeElapsed.start();
	size_t currentItemIndex = 0;
	for (auto it = _source.begin(); it != _source.end() && !_cancelRequested; ++it, ++currentItemIndex, _userResponse = urNone /* needed for normal operation of condition variable  */)
	{
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

		QFileInfo destInfo(destination[currentItemIndex].absoluteFilePath(newFileName.isEmpty() ? it->fullName() : newFileName));
		if (destInfo.absoluteFilePath() == sourceFile.absoluteFilePath())
			continue;

		if (destInfo.exists() && destInfo.isFile())
		{
			// Global response registered
			if (_globalResponses.count(hrFileExists) > 0)
			{
				if (_globalResponses[hrFileExists] == urSkipAll)
					continue;
			}
			else if (_globalResponses.count(hrFileExists) <= 0 ||_globalResponses[hrFileExists] != urProceedWithAll)
			{
				_observer->onProcessHaltedCallback(hrFileExists, *it, CFileSystemObject(destInfo), QString());
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
					assert (((_userResponse == urProceedWithThis || _userResponse == urProceedWithAll) && _newName.isEmpty()) || (_userResponse == urRename && !_newName.isEmpty()) || (_globalResponses.count(hrFileExists) > 0 &&_globalResponses[hrFileExists] == urProceedWithAll));

				_userResponse = urNone;

				CFileSystemObject destFile(destInfo);
				if (!destFile.properties().permissions.write)
				{
					if (_globalResponses.count(hrDestFileIsReadOnly) <= 0 || _globalResponses[hrDestFileIsReadOnly] != urProceedWithAll)
					{
						_observer->onProcessHaltedCallback(hrSourceFileIsReadOnly, *it, CFileSystemObject(), QString());
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
							assert ((_userResponse == urProceedWithThis || _userResponse == urProceedWithAll) && _newName.isEmpty());

						if (!it->makeWritable())
						{
							qDebug() << "Error making file " << it->absoluteFilePath() << " writable";
							assert(false);
							_userResponse = urNone;
							continue;
						}
						_userResponse = urNone;
					}
				}

				if (destFile.remove() != rcOk)
				{
					qDebug() << "Error removing source file " << destFile.absoluteFilePath() << ", error: " << destFile.lastErrorMessage();
					assert(false);
				}
			}
		}

		if (_op	== operationMove)
		{
			if (!destination[currentItemIndex].exists())
			{
				const bool ok = destination[currentItemIndex].mkpath(".");
				if (!ok)
				{
					qDebug() << "Error creating path " << destination[currentItemIndex].absolutePath();
					assert(false);
					continue;
				}
			}

			if (it->isFile())
			{
				const FileOperationResultCode result = it->moveAtomically(destination[currentItemIndex].absolutePath() + '/', _newName.isEmpty() ? newFileName : _newName);
				if (result != rcOk)
				{
					qDebug() << "Error moving file " << it->absoluteFilePath() << " to " << destination[currentItemIndex].absolutePath() + (_newName.isEmpty() ? newFileName : _newName) << ", error: " << it->lastErrorMessage();
					assert(result != rcOk);
				}

				if (!it->properties().permissions.write)
				{
					if (_globalResponses.count(hrSourceFileIsReadOnly) > 0 && _globalResponses[hrSourceFileIsReadOnly] == urSkipAll)
					{
						continue;
					}
					else if (_globalResponses.count(hrSourceFileIsReadOnly) <= 0 || _globalResponses[hrSourceFileIsReadOnly] != urProceedWithAll)
					{
						_observer->onProcessHaltedCallback(hrSourceFileIsReadOnly, *it, CFileSystemObject(), QString());
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
							assert ((_userResponse == urProceedWithThis || _userResponse == urProceedWithAll) && _newName.isEmpty());

						if (!it->makeWritable())
						{
							qDebug() << "Error making file " << it->absoluteFilePath() << " writable";
							assert(false);
							_userResponse = urNone;
							continue;
						}
						_userResponse = urNone;
					}
				}
			}

			if (it->remove() != rcOk)
			{
				qDebug() << "Error removing file " << it->absoluteFilePath() << ", error: " << it->lastErrorMessage();
				assert(false);
			}
		}
		else if (_op == operationCopy)
		{
			if (!destination[currentItemIndex].exists())
			{
				const bool ok = destination[currentItemIndex].mkpath(".");
				if (!ok)
				{
					qDebug() << "Error creating path " << destination[currentItemIndex].absolutePath();
					assert(false);
					continue;
				}
			}

			if (it->isFile())
			{
				static const int chunkSize = 5 * 1024 * 1024;
				const QString destPath = destination[currentItemIndex].absolutePath() + '/';
				FileOperationResultCode result = rcFail;

				// For calculating speed
				_fileTimeElapsed.start();
				do
				{
					_fileTimeElapsed.pause();
					while (_paused)
						std::this_thread::sleep_for(std::chrono::milliseconds(100));

					_fileTimeElapsed.resume();
					result = it->copyChunk(chunkSize, destPath, _newName.isEmpty() ? newFileName : _newName);
					const int totalPercentage = totalSize > 0 ? static_cast<int>((sizeProcessed + it->bytesCopied()) * 100 / totalSize) : 0;
					const int filePercentage = it->size() > 0 ? static_cast<int>(it->bytesCopied() * 100 / it->size()) : 0;
					//TODO: smooth speed
					const uint64_t speed = _fileTimeElapsed.elapsed() > 0 ? it->bytesCopied() * 1000 / _fileTimeElapsed.elapsed() : 0;// B/s
					_observer->onProgressChangedCallback(totalPercentage, currentItemIndex, _source.size(), filePercentage, speed);

					if (_cancelRequested)
					{
						if (it->cancelCopy() != rcOk)
							assert(false);
						result = rcOk;
						break;
					}
				}
				while (result == rcOk && it->copyOperationInProgress());

				if (result != rcOk)
				{
					qDebug() << "Error copying file " << it->absoluteFilePath() << " to " << destPath + (_newName.isEmpty() ? newFileName : _newName) << ", error: " << it->lastErrorMessage();
					assert(result != rcOk);
				}
			}
		}
		else
		{
			assert (!"Illegal op");
		}

		sizeProcessed += it->size();
		_newName.clear();
	}

	finalize();
}

void COperationPerformer::deleteFiles()
{
	_inProgress = true;
	uint64_t totalSize = 0;
	auto destination = flattenSourcesAndCalcDest(totalSize);
	assert (destination.size() == _source.size());

	size_t currentItemIndex = 0;
	for (auto it = _source.begin(); it != _source.end() && !_cancelRequested; ++it, ++currentItemIndex, _userResponse = urNone /* needed for normal condition variable operation */)
	{
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

		if (it->isFile())
		{
			if (!it->properties().permissions.write)
			{
				if (_globalResponses.count(hrSourceFileIsReadOnly) > 0 && _globalResponses[hrSourceFileIsReadOnly] == urSkipAll)
				{
					continue;
				}
				else if (_globalResponses.count(hrSourceFileIsReadOnly) <= 0 || _globalResponses[hrSourceFileIsReadOnly] != urProceedWithAll)
				{
					_observer->onProcessHaltedCallback(hrSourceFileIsReadOnly, *it, CFileSystemObject(), QString());
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
						assert ((_userResponse == urProceedWithThis || _userResponse == urProceedWithAll) && _newName.isEmpty());

					if (!it->makeWritable())
					{
						// TODO: show a message
						qDebug() << "Error making file" << it->absoluteFilePath() << "writable";
						assert(false);
						_userResponse = urNone;
						continue;
					}
					_userResponse = urNone;
				}
			}
		}

		if (it->remove() != rcOk)
		{
			qDebug() << "Error removing" << (it->isFile() ? "file" : "folder") << it->absoluteFilePath() << ", error: " << it->lastErrorMessage();
			assert(false);
		}

		_observer->onProgressChangedCallback(int(currentItemIndex * 100 / _source.size()), currentItemIndex, _source.size(), 0, 0);
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
	const bool destIsFileName = _source.size() == 1 && _source.front().isFile() && !_dest.endsWith("/") && !_dest.endsWith("\\");
	for (auto& o: _source)
	{
		if (o.isFile())
		{
			totalSize += o.size();
			// Ignoring the new file name here if it was supplied. We're only calculating dest dir here, not the file name
			destinations.emplace_back(destinationFolder(o.absoluteFilePath(), o.parentDirPath(), destIsFileName ? CFileSystemObject(_dest).parentDirPath() : _dest, false));
			newSourceVector.push_back(o);
		}
		else if (o.isDir())
		{
			auto children = recurseDirectoryItems(o.absoluteFilePath(), true);
			for (auto& file : children)
			{
				totalSize += file.size();
				destinations.emplace_back(destinationFolder(file.absoluteFilePath(), o.parentDirPath(), _dest, file.isDir()));
				newSourceVector.push_back(file);
			}
			destinations.emplace_back(destinationFolder(o.absoluteFilePath(), o.parentDirPath(), _dest, true));
			newSourceVector.push_back(o);
		}
	};

	_source = newSourceVector;
	return destinations;
}

QDir destinationFolder(const QString &absoluteSourcePath, const QString &originPath, const QString &destPath, bool /*sourceIsDir*/)
{
	QString localPath = absoluteSourcePath.mid(originPath.length());
	assert(!localPath.isEmpty());
	if (localPath.startsWith('\\') || localPath.startsWith('/'))
		localPath.remove(0, 1);

	const QString tmp = QFileInfo(destPath).absoluteFilePath();
	assert(QString(destPath+"/").remove("//") == QString(tmp+"/").remove("//"));
	const QString result = destPath + "/" + localPath;
	return QFileInfo(result).absolutePath();
}
