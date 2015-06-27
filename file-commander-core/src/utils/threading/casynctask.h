#pragma once

#include <thread>
#include <assert.h>

class CAsyncTask
{
public:
	~CAsyncTask()
	{
		if (_thread.joinable())
			_thread.join();
	}

	template <class Callable, typename ...Args>
	void exec(Callable&& task, Args&&... args)
	{
		try
		{
			const auto workerTid = _thread.get_id(), thisTid = std::this_thread::get_id();
			if (workerTid != thisTid)
			{
				if (_thread.joinable())
					_thread.join();

				_thread = std::thread(std::forward<Callable>(task), std::forward<Args>(args)...);
			}
			else
				task(std::forward<Args>(args)...);
		}
		catch (std::exception& e)
		{
			assert(false);
			(void)e;
		}
	}

private:
	std::thread _thread;
};
