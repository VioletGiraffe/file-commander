#include "../ccontroller.h"

DISABLE_COMPILER_WARNINGS
#include <qhash.h>
RESTORE_COMPILER_WARNINGS

const int tag = abs((int)qHash(QString("CFileSearchEngine")));

CFileSearchEngine::CFileSearchEngine(CController& controller) :
	_controller(controller),
	_workerThread("File search thread")
{
}

void CFileSearchEngine::addListener(CFileSearchEngine::FileSearchListener* listener)
{
	assert_r(listener);
	_listeners.insert(listener);
}

void CFileSearchEngine::removeListener(CFileSearchEngine::FileSearchListener* listener)
{
	_listeners.erase(listener);
}

bool CFileSearchEngine::searchInProgress() const
{
	return _workerThread.running();
}

void CFileSearchEngine::search(const QString& what, const QString& where, const QString& contentsToFind)
{
	if (_workerThread.running())
	{
		_workerThread.interrupt();
		return;
	}

	if (what.isEmpty() || where.isEmpty())
		return;

	_workerThread.exec([this, where, what, contentsToFind](){

		const auto hierarchy = enumerateDirectoryRecursively(CFileSystemObject(where),
			[this, what](QString path){

			_controller.execOnUiThread([this, path, what](){
				for (const auto& listener: _listeners)
					listener->itemScanned(path);
			}, tag);

			if (path.contains(what))
				_controller.execOnUiThread([this, path, what](){
				for (const auto& listener : _listeners)
					listener->matchFound(path);
			});

		}, _workerThread.terminationFlag());

		_controller.execOnUiThread([this](){
			for (const auto& listener: _listeners)
				listener->searchFinished();
		});
	});
}

void CFileSearchEngine::stopSearching()
{
	_workerThread.interrupt();
}

