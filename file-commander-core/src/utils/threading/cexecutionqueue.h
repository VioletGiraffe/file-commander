#pragma once

#include <deque>
#include <functional>
#include <mutex>

// A thread-safe class for delayed code execution, useful for cross-thread execution / communication
class CExecutionQueue
{
	struct Executee
	{
		//Executee(const std::function<void ()>& code_, int tag = 0)
		int tag;
		std::function<void ()> code;
	};


public:
	enum ExecutionMode {execFirst, execAll};

	inline void enqueue(const std::function<void ()>& code, int tag = -1)
	{
		std::lock_guard<std::mutex> locker(_queueMutex);
		const auto existingExecutee = tag == -1 ? _queue.end() : std::find_if(_queue.begin(), _queue.end(), [tag](const Executee& e){return e.tag == tag;});
		if (existingExecutee == _queue.end())
			_queue.push_back({ tag, code });
		else
			existingExecutee->code = code;
	}

	inline void exec(ExecutionMode mode = execAll)
	{
		Executee queueItem;
		while (try_pop(queueItem))
		{
			if (queueItem.code)
				queueItem.code();

			if (mode == execFirst)
				return;
		}
	}

private:
	inline bool try_pop(Executee& e)
	{
		std::lock_guard<std::mutex> locker(_queueMutex);
		if (!_queue.empty())
		{
			e = _queue.front();
			_queue.pop_front();
			return true;
		}

		return false;
	}

private:
	std::deque<Executee> _queue;
	std::mutex           _queueMutex;
};

