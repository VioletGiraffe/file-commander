#pragma once

// Helpers shared by all file-operation test .cpp files.
// Includes catch.hpp: the runner TU must #define CATCH_CONFIG_RUNNER before including this header.

#include "fileoperations/cfilesystemmutator.h"
#include "fileoperations/coperationexecutioncontext.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <stdint.h>
#include <stdlib.h> // rand()

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

inline bool createHardLink(const QString& existingPath, const QString& linkPath)
{
#ifdef _WIN32
	return CreateHardLinkW(reinterpret_cast<const WCHAR*>(linkPath.utf16()), reinterpret_cast<const WCHAR*>(existingPath.utf16()), nullptr) != 0;
#else
	return ::link(QFile::encodeName(existingPath).constData(), QFile::encodeName(linkPath).constData()) == 0;
#endif
}

// Creates the platform's common directory link type: a symlink on POSIX, a junction on Windows
// (junction creation, unlike symlink creation, requires no special privileges on Windows)
inline bool createDirectoryLink(const QString& targetPath, const QString& linkPath)
{
#ifdef _WIN32
	return QProcess::execute(QStringLiteral("cmd"), { QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
		QDir::toNativeSeparators(linkPath), QDir::toNativeSeparators(targetPath) }) == 0;
#else
	return QFile::link(targetPath, linkPath);
#endif
}

inline CEntryPath ep(const QString& text)
{
	const auto path = parseOperationPath(text);
	REQUIRE(path.has_value());
	return *path;
}

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

inline void buildRandomTree(const QString& dir, const int remainingDepth)
{
	const int childCount = 1 + rand() % 5;
	for (int i = 0; i < childCount; ++i)
	{
		if (remainingDepth > 0 && rand() % 3 == 0)
		{
			const QString subdir = dir % "/dir" % QString::number(i);
			REQUIRE(QDir{}.mkpath(subdir));
			buildRandomTree(subdir, remainingDepth - 1);
		}
		else
			writeTestFile(dir % "/file" % QString::number(i) % ".bin", patternedContents(rand() % 5000));
	}
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

// Compares the followed shapes and file contents; exactly right for materializing transfers, where source
// links become real destination entries with the target content.
inline void requireEqualTrees(const QString& expectedDir, const QString& actualDir)
{
	const auto filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
	const QStringList expected = QDir{ expectedDir }.entryList(filters, QDir::Name);
	const QStringList actual = QDir{ actualDir }.entryList(filters, QDir::Name);
	REQUIRE(expected == actual);

	for (const QString& name : expected)
	{
		const QString expectedPath = expectedDir % '/' % name;
		const QString actualPath = actualDir % '/' % name;
		REQUIRE(QFileInfo{ expectedPath }.isDir() == QFileInfo{ actualPath }.isDir());
		if (QFileInfo{ expectedPath }.isDir())
			requireEqualTrees(expectedPath, actualPath);
		else
			REQUIRE(readFileContents(expectedPath) == readFileContents(actualPath));
	}
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
