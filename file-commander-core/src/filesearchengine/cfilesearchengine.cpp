#include "../ccontroller.h"
#include "system/ctimeelapsed.h"
#include "directoryscanner.h"

#include "qtcore_helpers/qstring_helpers.hpp"

#include "hash/jenkins_hash.hpp"
#include "threading/thread_helpers.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QRegularExpression>
#include <QTextStream>
RESTORE_COMPILER_WARNINGS

static QString queryToRegex(const QString& query)
{
	// Escape the dots
	QString regExString = QString{ query }.replace('.', QLatin1StringView{ "\\." });

	const bool nameQueryHasWildcards = query.contains('*') || query.contains('?');
	if (nameQueryHasWildcards)
	{
		regExString.replace('?', '.').replace('*', ".*");
		regExString.prepend("\\A").append("\\z");
	}

	if (!regExString.startsWith('^'))
		regExString.prepend('^');

	if (!regExString.endsWith('$'))
		regExString.append('$');

	return regExString;
}

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

bool CFileSearchEngine::search(const QString& what, bool subjectCaseSensitive, const QStringList& where, const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords)
{
	if (_workerThread.running())
	{
		_workerThread.interrupt();
		return true;
	}

	if (what.isEmpty() || where.empty())
		return false;

	_workerThread.exec([=, this] {
		searchThread(what, subjectCaseSensitive, where, contentsToFind, contentsCaseSensitive, contentsWholeWords);
	});

	return true;
}

void CFileSearchEngine::stopSearching()
{
	_workerThread.interrupt();
}

void CFileSearchEngine::searchThread(const QString& what, bool subjectCaseSensitive, const QStringList& where, const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords) noexcept
{
	::setThreadName("File search engine thread");

	uint64_t itemCounter = 0;
	CTimeElapsed timer;
	timer.start();

	const bool noFileNameFilter = (what == '*' || what.isEmpty());

	QRegularExpression queryRegExp;
	if (!noFileNameFilter)
	{
		queryRegExp.setPattern(queryToRegex(what));
		assert_r(queryRegExp.isValid());

		if (!subjectCaseSensitive)
			queryRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
	}

	const bool searchByContents = !contentsToFind.isEmpty();

	QRegularExpression fileContentsRegExp;
	if (searchByContents)
	{
		if (contentsToFind.contains(QRegularExpression(QSL("[*?]"))))
		{
			fileContentsRegExp.setPattern(QRegularExpression::wildcardToRegularExpression(contentsToFind));
			assert_r(fileContentsRegExp.isValid());
		}
		else if (contentsWholeWords)
		{
			fileContentsRegExp.setPattern("\\b" + contentsToFind + "\\b");
			assert_r(fileContentsRegExp.isValid());
		}

		if (!contentsCaseSensitive)
			fileContentsRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
	}

	const bool useFileContentsRegExp = !fileContentsRegExp.pattern().isEmpty();

	const int uniqueJobTag = static_cast<int>(jenkins_hash("CFileSearchEngine")) + rand();

	QString line;

	for (const QString& pathToLookIn : where)
	{
		scanDirectory(CFileSystemObject(pathToLookIn),
			[&](const CFileSystemObject& item) {

				++itemCounter;

				if (itemCounter % 8192 == 0)
				{
					// No need to report every single item and waste CPU cycles
					_controller.execOnUiThread([this, path{item.fullAbsolutePath()}]() {
						for (const auto& listener : _listeners)
							listener->itemScanned(path);
						}, uniqueJobTag);
				}

				const bool nameMatches = noFileNameFilter || queryRegExp.match(item.fullName()).hasMatch();

				if (!nameMatches)
					return;

				bool matchFound = false;

				if (searchByContents)
				{
					QFile file{ item.fullAbsolutePath() };
					if (!file.open(QFile::ReadOnly))
						return;

					const auto contentsCaseSensitivity = contentsCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

					QTextStream stream{ &file };

					while (!matchFound && !_workerThread.terminationFlag() && stream.readLineInto(&line))
					{
						// contains() is faster than RegEx match (as of Qt 5.4.2, but this was for QRegExp, not tested with QRegularExpression)
						matchFound = useFileContentsRegExp ? fileContentsRegExp.match(line).hasMatch() : line.contains(contentsToFind, contentsCaseSensitivity);
					}
				}
				else
					matchFound = nameMatches;


				if (matchFound)
				{
					_controller.execOnUiThread([this, path{ item.fullAbsolutePath() }]() {
						for (const auto& listener : _listeners)
							listener->matchFound(path);
					});
				}

			}, _workerThread.terminationFlag());
	}

	const auto elapsedMs = timer.elapsed();
	_controller.execOnUiThread([this, elapsedMs, itemCounter]() {
		for (const auto& listener : _listeners)
			listener->searchFinished(_workerThread.terminationFlag() ? SearchCancelled : SearchFinished, itemCounter, elapsedMs);
	});
}
