#include "../ccontroller.h"
#include "system/ctimeelapsed.h"

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

void CFileSearchEngine::search(const QString& what, bool caseSensitive, const QString& where, const QString& contentsToFind)
{
	if (_workerThread.running())
	{
		_workerThread.interrupt();
		return;
	}

	if (what.isEmpty() || where.isEmpty())
		return;

	_workerThread.exec([this, what, caseSensitive, where, contentsToFind](){

		uint64_t itemCounter = 0;
		CTimeElapsed timer;
		timer.start();

		const bool wildcardMode = what.contains(QRegExp("[*?]"));
		QRegExp queryRegExp;
		if (wildcardMode)
		{
			queryRegExp.setPatternSyntax(QRegExp::Wildcard);
			queryRegExp.setPattern(what);
			queryRegExp.setCaseSensitivity(caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
		}

		const auto hierarchy = enumerateDirectoryRecursively(CFileSystemObject(where),
			[&](QString path){

			++itemCounter;

			_controller.execOnUiThread([this, path, what](){
				for (const auto& listener: _listeners)
					listener->itemScanned(path);
			}, tag);

			if ((wildcardMode && queryRegExp.exactMatch(path)) || (!wildcardMode && path.contains(what, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive)))
				_controller.execOnUiThread([this, path, what](){
				for (const auto& listener : _listeners)
					listener->matchFound(path);
			});

		}, _workerThread.terminationFlag());

		const uint32_t speed = timer.elapsed() > 0 ? static_cast<uint32_t>(itemCounter * 1000u / timer.elapsed()) : 0;
		_controller.execOnUiThread([this, speed](){
			for (const auto& listener: _listeners)
				listener->searchFinished(_workerThread.terminationFlag() ? SearchCancelled : SearchFinished, speed);
		});
	});
}

void CFileSearchEngine::stopSearching()
{
	_workerThread.interrupt();
}

