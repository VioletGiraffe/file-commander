#pragma once

#include "cconsumerblockingqueue.h"

#include <atomic>
#include <thread>
#include <assert.h>

class CWorkerThread
{
public:
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
			_terminate = true;
			_thread.join();
			_terminate = false;
		}
	}

	void exec(const std::function<void()>& task)
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
	std::atomic<bool> _working = false;
	std::atomic<bool> _terminate = false;
};
