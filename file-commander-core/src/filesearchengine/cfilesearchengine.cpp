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

	const bool nameQueryHasWildcards = what.contains(QRegularExpression(QSL("[*?]")));
	const bool noFileNameFilter = what == '*';

	QRegularExpression queryRegExp;
	if (!noFileNameFilter && nameQueryHasWildcards)
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

	QRegularExpression fileContentsRegExp;
	if (contentsToFind.contains(QRegularExpression(QSL("[*?]"))))
	{
		fileContentsRegExp.setPattern(QRegularExpression::wildcardToRegularExpression(contentsToFind));
		assert_r(fileContentsRegExp.isValid());
	}
	else if (contentsWholeWords)
		fileContentsRegExp = QRegularExpression{ "\\b" + contentsToFind + "\\b" };

	if (!contentsCaseSensitive)
		fileContentsRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);

	const bool useFileContentsRegExp = !fileContentsRegExp.pattern().isEmpty();

	const int uniqueJobTag = static_cast<int>(jenkins_hash("CFileSearchEngine")) + rand();

	QString line;

	for (const QString& pathToLookIn : where)
	{
		scanDirectory(CFileSystemObject(pathToLookIn),
			[&](const CFileSystemObject& item) {

				++itemCounter;

				const QString path = item.fullAbsolutePath();

				if (itemCounter % 8192 == 0)
				{
					// No need to report every single item and waste CPU cycles
					_controller.execOnUiThread([this, path, what]() {
						for (const auto& listener : _listeners)
							listener->itemScanned(path);
						}, uniqueJobTag);
				}

				// contains() is faster than RegEx match (as of Qt 5.4.2, but this was for QRegExp, not tested with QRegularExpression)
				if (noFileNameFilter
					|| (nameQueryHasWildcards && queryRegExp.match(path).hasMatch())
					|| (!nameQueryHasWildcards && path.contains(what, subjectCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive))
					)
				{
					QFile file(path);
					if (!contentsToFind.isEmpty())
					{
						if (!file.open(QFile::ReadOnly))
							return;
					}

					const auto contentsCaseSensitivity = contentsCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

					QTextStream stream{ &file };

					bool matchFound = contentsToFind.isEmpty();
					while (!matchFound && !_workerThread.terminationFlag() && stream.readLineInto(&line))
					{
						// contains() is faster than RegEx match (as of Qt 5.4.2, but this was for QRegExp, not tested with QRegularExpression)
						matchFound = useFileContentsRegExp ? fileContentsRegExp.match(line).hasMatch() : line.contains(contentsToFind, contentsCaseSensitivity);
					}

					if (matchFound)
					{
						_controller.execOnUiThread([this, path, what]() {
							for (const auto& listener : _listeners)
								listener->matchFound(path);
						});
					}
				}
			}, _workerThread.terminationFlag());
	}

	const auto elapsedMs = timer.elapsed();
	const uint32_t speed = elapsedMs > 0 ? static_cast<uint32_t>(itemCounter * 1000u / elapsedMs) : 0;
	_controller.execOnUiThread([this, speed]() {
		for (const auto& listener : _listeners)
			listener->searchFinished(_workerThread.terminationFlag() ? SearchCancelled : SearchFinished, speed);
	});
}
