#pragma once

#include "compiler/compiler_warnings_control.h"
#include "threading/cinterruptablethread.h"

DISABLE_COMPILER_WARNINGS
#include <QObject>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <list>

// Runs blocking OS shell operations (delete, clipboard paste), one dedicated thread each. They put up the shell's
// own progress and prompt UI on the calling thread and return only when it is dismissed, so their duration is
// unbounded - a fixed-size pool would let one of them stall the rest.
class CShellOperationRunner final : public QObject
{
public:
	explicit CShellOperationRunner(QObject* parent = nullptr) : QObject(parent) {}
	~CShellOperationRunner() override;

	// Call on the UI thread. The operation gets no completion callback: whatever it needs to report, it marshals
	// back itself with a queued invocation targeting an object whose death should cancel the report.
	void run(std::function<void()> operation);

	// Joins everything still running. Idempotent, and the destructor calls it, but the join belongs wherever the
	// window that owns the operations' shell dialogs is still alive - by the destructor it may not be.
	void waitForPendingOperations();

private:
	// UI thread only - run() and the queued reaping both happen there, which is what keeps this lock-free.
	// std::list: entries are addressed by iterator, and CInterruptableThread is neither copyable nor movable.
	std::list<CInterruptableThread> _operations;
};
