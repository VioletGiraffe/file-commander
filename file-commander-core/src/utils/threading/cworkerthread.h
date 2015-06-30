#pragma once

#include "cconsumerblockingqueue.h"

#include <assert.h>
#include <atomic>
#include <string>
#include <thread>

class CWorkerThread
{
public:
	explicit CWorkerThread(const std::string& threadName) : _threadName(threadName)
	{
	}

	CWorkerThread(const CWorkerThread&) = delete;
	CWorkerThread& operator=(const CWorkerThread&) = delete;

	~CWorkerThread()
	{
		stop();
	}

	void start()
	{
		if (_working)
			return;

		_working = true;
		_thread = std::thread(&CWorkerThread::threadFunc, this);
	}

	void stop()
	{
		if (_working)
		{
			// join() may throw
			try {
				_terminate = true;
				_queue.push(std::function<void()>()); // Pushing a dummy non-executable item in the task queue so that pop() wakes up and the thread may resume in order to finish
				_thread.join();
				_terminate = false;
			}
			catch (std::exception& e)
			{
				assert(!"Exception caught in CWorkerThread::stop()");
				(void)e;
			}
		}
	}

	void enqueue(const std::function<void()>& task)
	{
		_queue.push(task);
		start();
	}

private:
	void threadFunc()
	{
		_working = true;
		while (!_terminate)
		{
			std::function<void()> task;
			_queue.pop(task);
			if (task)
				task();
		}
		_working = false;
	}

private:
	CConsumerBlockingQueue<std::function<void()>> _queue;
	std::thread _thread;
	std::atomic<bool> _working {false};
	std::atomic<bool> _terminate {false};
	std::string _threadName;
};
