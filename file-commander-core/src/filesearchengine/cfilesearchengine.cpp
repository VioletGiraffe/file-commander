#include "../ccontroller.h"
#include "system/ctimeelapsed.h"
#include "directoryscanner.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <qhash.h>
#include <QRegularExpression>
#include <QTextStream>
RESTORE_COMPILER_WARNINGS

const int tag = abs((int)qHash(QString("CFileSearchEngine")));

CFileSearchEngine::CFileSearchEngine(CController& controller) :
	_controller(controller),
	_workerThread("File search thread")
{
}

void CFileSearchEngine::addListener(CFileSearchEngine::FileSearchListener* listener)
{
	assert_and_return_r(listener, );
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

void CFileSearchEngine::search(const QString& what, bool subjectCaseSensitive, const QStringList& where, const QString& contentsToFind, bool contentsCaseSensitive)
{
	if (_workerThread.running())
	{
		_workerThread.interrupt();
		return;
	}

	if (what.isEmpty() || where.empty())
		return;

	_workerThread.exec([this, what, subjectCaseSensitive, where, contentsToFind, contentsCaseSensitive](){
		uint64_t itemCounter = 0;
		CTimeElapsed timer;
		timer.start();

		const bool nameQueryHasWildcards = what.contains(QRegularExpression("[*?]"));
		QRegularExpression queryRegExp;
		if (nameQueryHasWildcards)
		{
			QString adjustedQuery = what;
			if (!what.startsWith('*'))
				adjustedQuery.prepend('*');
			if (!what.endsWith('*'))
				adjustedQuery.append('*');

			queryRegExp.setPattern(QRegularExpression::wildcardToRegularExpression(adjustedQuery));
			assert_r(queryRegExp.isValid());
			if (!subjectCaseSensitive)
				queryRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
		}



		for (const QString& pathToLookIn: where)
		{
			scanDirectory(CFileSystemObject(pathToLookIn),
				[&](const CFileSystemObject& item) {

				++itemCounter;

				const QString path = item.fullAbsolutePath();
				_controller.execOnUiThread([this, path, what](){
					for (const auto& listener: _listeners)
						listener->itemScanned(path);
				}, tag);

				// contains() is faster than RegEx match (as of Qt 5.4.2, but this was for QRegExp, not tested with QRegularExpression)
				if ((nameQueryHasWildcards && queryRegExp.match(path).hasMatch()) || (!nameQueryHasWildcards && path.contains(what, subjectCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive)))
				{
					QFile file(path);
					if (!contentsToFind.isEmpty())
					{
						if (!file.open(QFile::ReadOnly))
							return;
					}

					QTextStream stream(&file);
					const bool contentsQueryHasWildcards = contentsToFind.contains(QRegularExpression("[*?]"));
					const auto subjectCaseSensitivity = subjectCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
					QRegularExpression fileContentsRegExp;
					if (contentsQueryHasWildcards)
					{
						fileContentsRegExp.setPattern(QRegularExpression::wildcardToRegularExpression(contentsToFind));
						assert_r(fileContentsRegExp.isValid());
						if (!contentsCaseSensitive)
							fileContentsRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
					}

					bool matchFound = contentsToFind.isEmpty();
					while (!matchFound && !_workerThread.terminationFlag() && !stream.atEnd())
					{
						const QString line = stream.readLine();
						// contains() is faster than RegEx match (as of Qt 5.4.2, but this was for QRegExp, not tested with QRegularExpression)
						matchFound = contentsQueryHasWildcards ? fileContentsRegExp.match(line).hasMatch() : line.contains(contentsToFind, subjectCaseSensitivity);
					}

					if (matchFound)
					{
						_controller.execOnUiThread([this, path, what](){
						for (const auto& listener : _listeners)
							listener->matchFound(path);
						});
					}
				}
			}, _workerThread.terminationFlag());
		}

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

