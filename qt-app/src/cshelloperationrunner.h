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

	// Returns once everything still running has finished, dispatching events meanwhile. Call it from a point
	// where pumping the message loop is safe, and while the window owning the operations' shell dialogs is alive.
	void waitForPendingOperations();

private:
	// UI thread only - run(), the wait, and the queued reaping all happen there, which is what keeps this lock-free.
	// std::list: entries are addressed by iterator, and CInterruptableThread is neither copyable nor movable.
	std::list<CInterruptableThread> _operations;
};
