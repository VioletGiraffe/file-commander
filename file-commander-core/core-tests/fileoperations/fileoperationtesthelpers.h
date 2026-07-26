#pragma once

// Helpers shared by all file-operation test .cpp files.
// Includes catch.hpp: the runner TU must #define CATCH_CONFIG_RUNNER before including this header.

#include "fileoperations/cfilesystemmutator.h"
#include "fileoperations/coperationexecutioncontext.h"
#include "filecomparator/foldercomparison.h"

#include "link_helpers.hpp"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <stdint.h>
#include <string>

#include "3rdparty/catch2/catch.hpp"

// Settable via the --std-seed command-line option; see main.cpp.
extern uint32_t g_randomSeed;

// One scripted execution environment for the executor tests: pre-scripted decisions, captured requests
// and progress, and hooks for exact-point intervention.
struct OperationScript
{
	std::vector<Decision> decisions;
	size_t nextDecision = 0;
	std::vector<DecisionRequest> seenRequests;
	std::vector<ProgressSnapshot> progress;
	bool cancelInsteadOfAnswering = false;
	// Run mid-operation, for filesystem changes at exact execution points: at every checkpoint (before
	// the cancellation predicate), and at every decision request before the scripted answer.
	std::function<void()> onCheckpoint;
	std::function<void(const DecisionRequest&)> onDecisionRequest;
	// Cancellation is requested through observable state, never by counting checkpoint calls (call order
	// is an implementation detail): the operation cancels at the first checkpoint where this holds.
	std::function<bool()> cancelAtCheckpoint;
};

inline COperationExecutionContext makeScriptedContext(OperationScript& script, const PrimaryProgressUnit primaryUnit)
{
	return COperationExecutionContext{
		primaryUnit,
		[&script] {
			if (script.onCheckpoint)
				script.onCheckpoint();
			return !script.cancelAtCheckpoint || !script.cancelAtCheckpoint();
		},
		[&script](const DecisionRequest& request) -> std::optional<Decision> {
			script.seenRequests.push_back(request);
			if (script.onDecisionRequest)
				script.onDecisionRequest(request);
			if (script.cancelInsteadOfAnswering)
				return {};
			REQUIRE(script.nextDecision < script.decisions.size());
			return script.decisions[script.nextDecision++];
		},
		[&script](const ProgressSnapshot& snapshot) { script.progress.push_back(snapshot); }
	};
}

inline Decision act(const DecisionAction action, const DecisionScope scope = DecisionScope::ThisItem)
{
	return Decision{ action, scope, {} };
}

inline void writeTestFile(const QString& path, const QByteArray& contents)
{
	QFile file(path);
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write(contents) == contents.size());
}

inline QByteArray readFileContents(const QString& path)
{
	QFile file(path);
	REQUIRE(file.open(QFile::ReadOnly));
	return file.readAll();
}

inline CEntryPath ep(const QString& text)
{
	const auto path = parseOperationPath(text);
	REQUIRE(path.has_value());
	return *path;
}

// These route through the production inspectEntry, so an armed Point::InspectEntry_Native forced error is
// consumed here too. Snapshot/assert absence before opening a CFaultHookScope that arms that point, or the
// helper eats the forced error the code under test was meant to see (and its REQUIRE trips on the failure).
inline EntrySnapshot snapshotOf(const QString& text)
{
	const auto result = inspectEntry(ep(text));
	REQUIRE(result.has_value());
	REQUIRE(result->has_value());
	return **result;
}

inline bool entryAbsent(const QString& text)
{
	const auto result = inspectEntry(ep(text));
	REQUIRE(result.has_value());
	return !result->has_value();
}

inline QByteArray patternedContents(const int size)
{
	QByteArray data(size, '\0');
	char* bytes = data.data();
	for (int i = 0; i < size; ++i)
		bytes[i] = static_cast<char>(i * 31 + 7);
	return data;
}

inline size_t countTreeEntries(const QString& dir)
{
	size_t count = 0;
	const auto filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
	for (const QString& name : QDir{ dir }.entryList(filters))
	{
		++count;
		if (QFileInfo{ dir % '/' % name }.isDir())
			count += countTreeEntries(dir % '/' % name);
	}
	return count;
}

inline const char* differenceName(const EntryDifference status)
{
	switch (status) // No default: a new EntryDifference value has to surface here as a compiler warning
	{
	case EntryDifference::OnlyInLeft: return "only in expected";
	case EntryDifference::OnlyInRight: return "only in actual";
	case EntryDifference::Different: return "differs";
	case EntryDifference::ComparisonFailed: return "unreadable";
	}

	return "?";
}

// Compares the followed shapes and file contents; exactly right for materializing transfers, where source
// links become real destination entries with the target content.
inline void requireEqualTrees(const QString& expectedDir, const QString& actualDir)
{
	// Exact pairing, spelled out: here a name differing only in letter case is a defect to catch, not a match.
	const auto comparison = compareFolders(expectedDir, actualDir, PairingMode::Exact);
	if (comparison.differences.empty())
		return;

	// Name every difference: a tree that came out wrong usually differs in more than one place, and the first
	// one alone seldom explains why.
	std::string report = "Trees differ.\n  expected: " + expectedDir.toStdString() + "\n  actual:   " + actualDir.toStdString();
	for (const FolderComparisonEntry& entry : comparison.differences)
		report += "\n  " + std::string{ differenceName(entry.status) } + ": " + entry.relativePath.toStdString();

	FAIL(report);
}

inline int stagingFileCount(const QString& directory)
{
	return static_cast<int>(QDir{ directory }.entryList({ QStringLiteral(".file-commander-copy-*") },
		QDir::Files | QDir::Hidden | QDir::System).size());
}

inline bool setEntryTimes(const QString& path, const thin_io::entry_times& times)
{
#ifdef _WIN32
	return thin_io::set_times(path.toStdWString().c_str(), times);
#else
	return thin_io::set_times(QFile::encodeName(path).constData(), times);
#endif
}

inline std::optional<thin_io::entry_times> getEntryTimes(const QString& path)
{
#ifdef _WIN32
	return thin_io::get_times(path.toStdWString().c_str());
#else
	return thin_io::get_times(QFile::encodeName(path).constData());
#endif
}

inline void setFileReadOnly(const QString& path, const bool readOnly)
{
	using P = QFileDevice::Permission;
	const QFileDevice::Permissions perms = readOnly
		? (P::ReadOwner | P::ReadUser | P::ReadGroup | P::ReadOther)
		: (P::ReadOwner | P::WriteOwner | P::ReadUser | P::WriteUser | P::ReadGroup | P::ReadOther);
	REQUIRE(QFile::setPermissions(path, perms));
}

// On POSIX, permission bits do not restrict root: the read-only remediation flows can never trigger.
inline bool readOnlySemanticsUnavailable()
{
#ifndef _WIN32
	if (::geteuid() == 0)
	{
		WARN("Running as root: file permissions do not apply, read-only sections skipped");
		return true;
	}
#endif
	return false;
}

inline int64_t entryLastWriteSeconds(const QString& path)
{
	const auto times = getEntryTimes(path);
	REQUIRE(times.has_value());
	REQUIRE(times->last_write.has_value());
	return times->last_write->seconds;
}
