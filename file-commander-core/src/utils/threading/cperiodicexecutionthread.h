#pragma once

#include <atomic>
#include <limits>
#include <string>
#include <thread>

class CPeriodicExecutionThread
{
public:
	explicit CPeriodicExecutionThread(unsigned int period_ms, const std::string& threadName, const std::function<void ()>& workload = std::function<void ()>());

	CPeriodicExecutionThread(const CPeriodicExecutionThread&) = delete;
	CPeriodicExecutionThread& operator=(const CPeriodicExecutionThread&) = delete;

	~CPeriodicExecutionThread();

	void setWorkload(const std::function<void ()>& workload);

	void start(const std::function<void ()>& workload = std::function<void ()>());

	void terminate();

private:
	void threadFunc();

private:
	std::function<void ()> _workload;
	std::thread            _thread;
	std::string            _threadName;
	const unsigned int     _period = std::numeric_limits<unsigned int>::max(); // milliseconds

	std::atomic<bool>      _terminate {false};
};

