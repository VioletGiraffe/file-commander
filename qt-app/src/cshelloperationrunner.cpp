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
	waitForPendingOperations(); // Backstop; main() joins earlier, while the main window is still up
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
	_operations.clear(); // Each entry's destructor joins its thread

	// The threads posted their reaping invocations on the way out, and those name entries that no longer exist.
	QCoreApplication::removePostedEvents(this);
}
