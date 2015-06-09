#pragma once

#include <future>

template <typename ResultType>
class CAsyncTask
{
public:
	template <class Callable, typename ...Args>
	void exec(Callable&& task, Args&&... args)
	{
		if (_future.valid())
			_future.wait();

		_future = std::async(std::launch::async, std::forward<Callable>(task), std::forward<Args>(args)...);
	}

	ResultType getResult()
	{
		if (_future.valid())
			return _future.get();
		else
			return ResultType();
	}

private:
	std::future<ResultType> _future;
};