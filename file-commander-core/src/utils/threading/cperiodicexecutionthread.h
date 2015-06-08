#pragma once

#include <assert.h>
#include <atomic>
#include <limits>
#include <thread>

class CPeriodicExecutionThread
{
public:
	explicit inline CPeriodicExecutionThread(int period_ms, const std::function<void ()>& workload = std::function<void ()>()) : _workload(workload), _period(period_ms)
	{
	}

	inline ~CPeriodicExecutionThread()
	{
		terminate();
	}

	inline void setWorkload(const std::function<void ()>& workload)
	{
		if (!_thread.joinable())
			_workload = workload;
		else
			assert(!"The thread has already started");
	}

	inline void start(const std::function<void ()>& workload = std::function<void ()>())
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

	inline void terminate()
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
			qDebug() << __FUNCTION__ << "Exception caught:" << e.what();
		}
	}

private:
	inline void threadFunc()
	{
		if (!_workload)
			return;

		_threadRunning = true;

		while (!_terminate) // Main threadFunc loop
		{
			_workload();

			std::this_thread::sleep_for(std::chrono::milliseconds(_period));
		}

		_threadRunning = true;
	}

private:
	std::function<void ()> _workload;
	std::thread            _thread;
	const int              _period = std::numeric_limits<int>::max(); // milliseconds

	bool       _terminate = false;
	std::atomic<bool>       _threadRunning = false;
};

