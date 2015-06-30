#pragma once

#include <queue>
#include <thread>
#include <limits>
#include <mutex>
#include <condition_variable>

template <typename T>
class CConsumerBlockingQueue
{
public:
	explicit CConsumerBlockingQueue (size_t maxSize = std::numeric_limits<size_t>::max());
	void push (const T& element);
	// Non-blocking
	bool try_pop (T& element);
	// Blocking
	void pop (T& receiver);

	size_t size() const;

private:
	CConsumerBlockingQueue<T>& operator=(const CConsumerBlockingQueue<T>& other) { _queue = other._queue; return *this; }
	CConsumerBlockingQueue(const CConsumerBlockingQueue<T>& other) : _queue(other._queue) { }

private:
	const size_t              _maxSize;
	mutable std::mutex        _mutex;
	std::condition_variable   _cond;
	std::queue<T>             _queue;
};

template <typename T>
CConsumerBlockingQueue<T>::CConsumerBlockingQueue(size_t maxSize) : _maxSize(maxSize)
{
}

template <typename T>
size_t CConsumerBlockingQueue<T>::size() const
{
	std::unique_lock<std::mutex> locker(_mutex);
	return _queue.size();
}

// Non-blocking access
template <typename T>
bool CConsumerBlockingQueue<T>::try_pop(T& element)
{
	std::lock_guard<std::mutex> locker(_mutex);
	if (_queue.empty())
		return false;

	element = std::move(_queue.front());
	_queue.pop();
	return true;
}

// Blocking access
template <typename T>
void CConsumerBlockingQueue<T>::pop(T& receiver)
{
	std::unique_lock<std::mutex> lock(_mutex);
	while(_queue.empty())
		_cond.wait(lock);

	receiver = std::move(_queue.front());
	_queue.pop();
}

template <typename T>
void CConsumerBlockingQueue<T>::push( const T& element )
{
	while (_queue.size() > _maxSize) // Block until there's space in queue. Dangerous?
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	{
		std::unique_lock<std::mutex> lock(_mutex);
		_queue.push(element);
		lock.unlock();
		_cond.notify_one();
	}
}
