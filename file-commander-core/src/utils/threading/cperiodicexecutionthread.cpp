#include "cperiodicexecutionthread.h"
#include "utils/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS


#include <assert.h>

CPeriodicExecutionThread::CPeriodicExecutionThread(unsigned int period_ms, const std::string& threadName, const std::function<void()>& workload /*= std::function<void ()>()*/) :
	_workload(workload),
	_threadName(threadName),
	_period(period_ms)
{

}

CPeriodicExecutionThread::~CPeriodicExecutionThread()
{
	terminate();
}

void CPeriodicExecutionThread::setWorkload(const std::function<void()>& workload)
{
	if (!_thread.joinable())
		_workload = workload;
	else
		assert(!"The thread has already started");
}

void CPeriodicExecutionThread::start(const std::function<void()>& workload /*= std::function<void ()>()*/)
{
	if (!_thread.joinable())
	{
		if (workload)
			_workload = workload;

		_thread = std::thread(&CPeriodicExecutionThread::threadFunc, this);
	}
	else
		assert(!"The thread has already started");
}

void CPeriodicExecutionThread::terminate()
{
	try
	{
		if (_thread.joinable())
		{
			_terminate = true;
			_thread.join();
			_terminate = false;
		}
	}
	catch (const std::exception& e)
	{
		_terminate = false;
		qDebug() << __FUNCTION__ << "exception caught:" << e.what();
	}
}

void CPeriodicExecutionThread::threadFunc()
{
	if (!_workload)
		return;

	qDebug() << "Starting CPeriodicExecutionThread" << QString::fromStdString(_threadName);

	while (!_terminate) // Main threadFunc loop
	{
		_workload();

		std::this_thread::sleep_for(std::chrono::milliseconds(_period));
	}

	qDebug() << "CPeriodicExecutionThread" << QString::fromStdString(_threadName) << "finished and exiting";
}
