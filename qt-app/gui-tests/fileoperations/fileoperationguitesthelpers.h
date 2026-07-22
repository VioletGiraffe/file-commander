#pragma once

// Shared helpers for the file-operation gui-tests: deterministic file/path builders, an event-loop pump, and a
// CFileOperationDialog subclass that answers decision requests from a script instead of a modal prompt and
// exposes the protected rendering/drain seams. Used by the dialog tests and the WP11A production-routing gate.

#include "progressdialogs/cfileoperationdialog.h"
#include "fileoperations/centrypath.h"
#include "fileoperations/fileoperationtypes.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include "3rdparty/catch2/catch.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

namespace guitest
{

inline void writeFile(const QString& path, const QByteArray& contents)
{
	QFile file{ path };
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write(contents) == contents.size());
}

inline QByteArray readFile(const QString& path)
{
	QFile file{ path };
	REQUIRE(file.open(QFile::ReadOnly));
	return file.readAll();
}

// Deterministic, position-dependent bytes so a copy's fidelity can be verified by value.
inline QByteArray blob(const int size)
{
	QByteArray data(size, '\0');
	for (int i = 0; i < size; ++i)
		data[i] = static_cast<char>((i * 37 + 11) & 0xFF);
	return data;
}

inline CEntryPath entryPath(const QString& text)
{
	auto path = parseOperationPath(text);
	REQUIRE(path.has_value());
	return *path;
}

// Pumps the main-thread event loop (so the dialog's timer fires and its queue drains) until the condition holds
// or the deadline passes.
[[nodiscard]] inline bool pumpUntil(const std::function<bool()>& condition, const std::chrono::milliseconds timeout = std::chrono::seconds{ 10 })
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (!condition())
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		if (std::chrono::steady_clock::now() > deadline)
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds{ 2 });
	}
	return true;
}

// Answers decision requests from a script instead of showing a modal prompt, and exposes the protected
// rendering/drain seams so tests can drive them directly. An unscripted prompt fails the test.
class ScriptedDialog final : public CFileOperationDialog
{
public:
	using CFileOperationDialog::CFileOperationDialog;
	using CFileOperationDialog::drainEvents;
	using CFileOperationDialog::renderProgress;

	std::vector<Decision> scriptedDecisions;
	size_t nextDecision = 0;
	int decisionRequestsPresented = 0;
	std::optional<IssueKind> lastRequestKind;

protected:
	Decision presentDecision(const DecisionRequest& request) override
	{
		++decisionRequestsPresented;
		lastRequestKind = request.issue.kind;
		REQUIRE(nextDecision < scriptedDecisions.size());
		return scriptedDecisions[nextDecision++];
	}
};

inline QLabel* label(const CFileOperationDialog& dialog, const char* name)
{
	auto* found = dialog.findChild<QLabel*>(QLatin1String(name));
	REQUIRE(found != nullptr);
	return found;
}

} // namespace guitest
