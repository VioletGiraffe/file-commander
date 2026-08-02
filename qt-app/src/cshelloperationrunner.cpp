#include "cshelloperationrunner.h"

#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QThread>
RESTORE_COMPILER_WARNINGS

#include <utility>

CShellOperationRunner::~CShellOperationRunner()
{
	// Last resort: a bare join hangs an operation that still needs this thread, hence the obligation to call
	// waitForPendingOperations() earlier, while a message loop can still run.
	_operations.clear();
}

void CShellOperationRunner::run(std::function<void()> operation)
{
	assert_debug_only(QThread::currentThread() == thread()); // The list is unguarded; see its declaration

	CInterruptableThread& operationThread = _operations.emplace_back("Shell operation");
	const auto entry = std::prev(_operations.end());

	operationThread.start([this, entry, op = std::move(operation)](const std::atomic<bool>&) {
		// On scope exit rather than after the call: CInterruptableThread contains exceptions, so a throwing
		// operation would otherwise leave its entry behind for the rest of the session.
		EXEC_ON_SCOPE_EXIT([this, entry] {
			QMetaObject::invokeMethod(this, [this, entry] { _operations.erase(entry); }, Qt::QueuedConnection);
		});

		op();
	});
}

void CShellOperationRunner::waitForPendingOperations()
{
	assert_debug_only(QThread::currentThread() == thread());

	// Not a join: an operation is not done with this thread once its own UI is gone. Its shell dialogs are owned
	// by a window living here, and a paste marshals every clipboard IDataObject call into this thread's COM
	// apartment - both travel by window message, so blocking here instead of dispatching would deadlock it.
	while (!_operations.empty())
		QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
}
