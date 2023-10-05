#include "../ccontroller.h"
#include "system/ctimeelapsed.h"
#include "directoryscanner.h"

#include "qtcore_helpers/qstring_helpers.hpp"

#include "hash/jenkins_hash.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QRegularExpression>
#include <QTextStream>
RESTORE_COMPILER_WARNINGS

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

bool CFileSearchEngine::search(const QString& what, bool subjectCaseSensitive, const QStringList& where, const QString& contentsToFind, bool contentsCaseSensitive)
{
	if (_workerThread.running())
	{
		_workerThread.interrupt();
		return true;
	}

	if (what.isEmpty() || where.empty())
		return false;

	_workerThread.exec([this, what, subjectCaseSensitive, where, contentsToFind, contentsCaseSensitive](){
		uint64_t itemCounter = 0;
		CTimeElapsed timer;
		timer.start();

		const bool nameQueryHasWildcards = what.contains(QRegularExpression(QSL("[*?]")));
		QRegularExpression queryRegExp;
		if (nameQueryHasWildcards)
		{
			QString adjustedQuery = what;
			if (!adjustedQuery.startsWith('*'))
				adjustedQuery.prepend('*');

			if (!adjustedQuery.endsWith('*'))
				adjustedQuery.append('*');

			auto regExString = QRegularExpression::wildcardToRegularExpression(adjustedQuery);
			regExString.replace(QSL(R"([^/\\]*)"), QSL(".*"));
			queryRegExp.setPattern(regExString);
			assert_r(queryRegExp.isValid());
			if (!subjectCaseSensitive)
				queryRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
		}

		static constexpr int uniqueJobTag = static_cast<int>(jenkins_hash("CFileSearchEngine"));

		for (const QString& pathToLookIn: where)
		{
			scanDirectory(CFileSystemObject(pathToLookIn),
				[&](const CFileSystemObject& item) {

				++itemCounter;

				const QString path = item.fullAbsolutePath();
				_controller.execOnUiThread([this, path, what](){
					for (const auto& listener: _listeners)
						listener->itemScanned(path);
				}, uniqueJobTag);

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
					const bool contentsQueryHasWildcards = contentsToFind.contains(QRegularExpression(QSL("[*?]")));
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

		const auto elapsedMs = timer.elapsed();
		const uint32_t speed = elapsedMs > 0 ? static_cast<uint32_t>(itemCounter * 1000u / elapsedMs) : 0;
		_controller.execOnUiThread([this, speed](){
			for (const auto& listener: _listeners)
				listener->searchFinished(_workerThread.terminationFlag() ? SearchCancelled : SearchFinished, speed);
		});
	});

	return true;
}

void CFileSearchEngine::stopSearching()
{
	_workerThread.interrupt();
}

