#pragma once

// Shared by every CFileSearchEngine test: a throwaway tree to search in, a listener that collects what the engine
// reports, and a runner that turns one query into one result.
// Includes catch.hpp: the runner TU must #define CATCH_CONFIG_RUNNER before including this header.

#include "filesearchengine/cfilesearchengine.h"

#include "qtcore_helpers/qstring_helpers.hpp"
#include "qt_helpers.hpp" // operator<< for QString, so Catch2 can print a path in a failure report

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include "3rdparty/catch2/catch.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

// The chunk size the engine reads a file's contents in. Content tests place their needles relative to it, so it
// has to stay in step with the engine's own constant.
inline constexpr qsizetype contentScanWindowSize = 64 * 1024;

// A throwaway directory tree. Paths come back POSIX-separated, matching what the engine reports.
class TempTree
{
public:
	TempTree()
	{
		REQUIRE(_dir.isValid());
		REQUIRE(!_dir.path().contains('\\'));
	}

	[[nodiscard]] QString path() const { return _dir.path(); }
	[[nodiscard]] QString path(const QString& relative) const { return _dir.path() % '/' % relative; }

	QString makeDir(const QString& relative)
	{
		const QString absolute = path(relative);
		REQUIRE(QDir{}.mkpath(absolute));
		return absolute;
	}

	// Intermediate directories are created as needed, so a test can name a nested file in one line.
	QString makeFile(const QString& relative, const QByteArray& contents = {})
	{
		const QString absolute = path(relative);
		REQUIRE(QDir{}.mkpath(QFileInfo{ absolute }.absolutePath()));

		QFile file{ absolute };
		REQUIRE(file.open(QFile::WriteOnly));
		REQUIRE(file.write(contents) == contents.size());
		return absolute;
	}

	// A file of exactly totalSize bytes whose only occurrence of needle begins at needleOffset. Padding is one
	// repeated character the needle may not contain, so no second, accidental occurrence exists anywhere.
	// Whether the padding is itself a word character decides what the whole-word option sees around the needle.
	QString makeFileWithNeedleAt(const QString& relative, const QByteArray& needle, qsizetype needleOffset, qsizetype totalSize,
		const char padding = paddingByte)
	{
		REQUIRE(needleOffset >= 0);
		REQUIRE(needleOffset + needle.size() <= totalSize);
		REQUIRE(!needle.contains(padding));

		QByteArray contents(totalSize, padding);
		contents.replace(needleOffset, needle.size(), needle);
		return makeFile(relative, contents);
	}

	static constexpr char paddingByte = 'x';

private:
	QTemporaryDir _dir;
};

// The engine reports a directory with a trailing separator and a file without, so no test has to spell either.
[[nodiscard]] inline QString withoutTrailingSlash(QString path)
{
	while (path.endsWith('/'))
		path.chop(1);

	return path;
}

// CFileSearchEngine::search()'s parameter list, named. The defaults describe the most common query: every name,
// no content filter.
struct SearchQuery
{
	QStringList roots;
	QStringList nameFilters{ QSL("*") };
	bool nameCaseSensitive = false;
	QString contents;
	bool contentsCaseSensitive = false;
	bool contentsWholeWords = false;
	bool contentsIsRegex = false;
};

struct SearchResult
{
	std::vector<QString> matches;
	CFileSearchEngine::SearchStatus status = CFileSearchEngine::SearchFinished;
	uint64_t itemsScanned = 0;
	uint64_t inconclusiveItems = 0; // Files whose contents could not be decided: unreadable, or the match was abandoned
	size_t finishedNotifications = 0; // Exactly one per accepted search
	size_t linkReachedMatches = 0; // Of the matches above, how many the engine flagged as found through a directory link

	[[nodiscard]] bool matched(const QString& path) const
	{
		return std::ranges::any_of(matches, [target = withoutTrailingSlash(path)](const QString& match) {
			return withoutTrailingSlash(match) == target;
		});
	}

	[[nodiscard]] size_t count() const { return matches.size(); }
};

// Collects what the engine reports. The callbacks arrive off the calling thread - matchFound from the search
// thread for a name search, and from the content pool's threads for a content search - so the state is guarded.
class CollectingListener final : public CFileSearchEngine::FileSearchListener
{
public:
	// Runs on the search thread for every item the engine reports scanning, letting a test reach into a search
	// that is still going. Install it before starting the search, and keep Catch2 assertions out of it: they
	// throw, and an exception here crosses a thread boundary that cannot carry it.
	void setItemScannedHook(std::function<void()> hook) { _itemScannedHook = std::move(hook); }

	void itemScanned(const QString& /*currentItem*/) override
	{
		if (_itemScannedHook)
			_itemScannedHook();
	}

	void matchFound(const QString& path, bool reachedThroughLink) override
	{
		std::lock_guard lock{ _mutex };
		_matches.push_back(path);
		if (reachedThroughLink)
			++_linkReachedMatches;
	}

	void searchFinished(CFileSearchEngine::SearchStatus status, uint64_t itemsScanned, uint64_t inconclusiveItems, uint64_t /*msElapsed*/) override
	{
		std::lock_guard lock{ _mutex };
		_status = status;
		_itemsScanned = itemsScanned;
		_inconclusiveItems = inconclusiveItems;
		++_finishedNotifications;
	}

	[[nodiscard]] SearchResult collect() const
	{
		std::lock_guard lock{ _mutex };
		return SearchResult{ _matches, _status, _itemsScanned, _inconclusiveItems, _finishedNotifications, _linkReachedMatches };
	}

	void clear()
	{
		std::lock_guard lock{ _mutex };
		_matches.clear();
		_status = CFileSearchEngine::SearchFinished;
		_itemsScanned = 0;
		_inconclusiveItems = 0;
		_finishedNotifications = 0;
		_linkReachedMatches = 0;
	}

private:
	mutable std::mutex _mutex;
	std::vector<QString> _matches;
	CFileSearchEngine::SearchStatus _status = CFileSearchEngine::SearchFinished;
	uint64_t _itemsScanned = 0;
	uint64_t _inconclusiveItems = 0;
	size_t _finishedNotifications = 0;
	size_t _linkReachedMatches = 0;
	std::function<void()> _itemScannedHook;
};

// One engine and its listener. run() is the whole story for most tests; the separate steps are for the ones that
// need to act while the search is still going.
class SearchRunner
{
public:
	void setItemScannedHook(std::function<void()> hook) { _listener.setItemScannedHook(std::move(hook)); }

	[[nodiscard]] bool start(const SearchQuery& query)
	{
		return _engine.search(query.nameFilters, query.nameCaseSensitive, query.roots,
			query.contents, query.contentsCaseSensitive, query.contentsWholeWords, query.contentsIsRegex, &_listener);
	}

	void stop() { _engine.stopSearching(); }

	[[nodiscard]] bool searchInProgress() const { return _engine.searchInProgress(); }

	// Joining is all the synchronization a test needs: the engine reports completion from the search thread as its
	// last act, so everything the search had to say has been said by the time this returns.
	[[nodiscard]] SearchResult finish()
	{
		_engine.waitForSearchToFinish();
		return _listener.collect();
	}

	[[nodiscard]] SearchResult run(const SearchQuery& query)
	{
		REQUIRE(start(query));
		return finish();
	}

	// One runner can serve several searches; what they report accumulates until this is called.
	void clearRecorded() { _listener.clear(); }

private:
	// The worker holds the listener pointer and is joined by the engine's destructor, so the listener has to be
	// destroyed after the engine - i.e. declared before it.
	CollectingListener _listener;
	CFileSearchEngine _engine;
};

// One search, start to finish, for the tests that need nothing from it while it runs.
[[nodiscard]] inline SearchResult runSearch(const SearchQuery& query)
{
	SearchRunner runner;
	return runner.run(query);
}
