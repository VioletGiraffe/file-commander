#include "ctimeelapsed.h"
#include <assert.h>

#ifdef _WIN32
#include <Windows.h>

class CTimePrivate
{
public:
	CTimePrivate ();
	// Stores current time stamp
	void captureTime ();
	// Returns difference between two time stamps in milliseconds
	int operator-(const CTimePrivate& start) const;

	CTimePrivate& operator=(const CTimePrivate& other);

private:
	LARGE_INTEGER _time;
	LARGE_INTEGER _freq;
	bool          _bIsValid;
};

//////////////////////////////////////////////////////
//                  CTimePrivate
//////////////////////////////////////////////////////

CTimePrivate::CTimePrivate () : _bIsValid(false)
{
}

// Stores current time stamp
void CTimePrivate::captureTime()
{
	_bIsValid = false;
	BOOL succ = QueryPerformanceFrequency(&_freq);
	if (!succ)
	{
		assert(!"Error calling QueryPerformanceFrequency");
		return;
	}

	succ = QueryPerformanceCounter(&_time);
	if (!succ)
	{
		assert(!"Error calling QueryPerformanceCounter");
		return;
	}

	_bIsValid = true;
}

// Returns difference between two time stamps in milliseconds
int CTimePrivate::operator-(const CTimePrivate& start) const
{
	assert(_bIsValid == bool("Invalid end time sample"));
	assert(start._bIsValid == bool("Invalid start time sample"));

	if (_freq.QuadPart != start._freq.QuadPart)
	{
		assert(!"Samples were taken at different frequencies");
		return -1;
	}

	int msec = -1;
	if (_bIsValid && start._bIsValid)
	{
		msec = static_cast<int>( (_time.QuadPart - start._time.QuadPart) * 1000 / _freq.QuadPart );
	}

	return msec;
}

CTimePrivate& CTimePrivate::operator=(const CTimePrivate& other)
{
	_freq     = other._freq;
	_time     = other._time;
	_bIsValid = other._bIsValid;
	return *this;
}

#else

#include <time.h>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

class CTimePrivate
{
public:
	CTimePrivate ();
	// Сохраняет текущее время в этот экземпляр CTime
	void captureTime ();
	// Возвращает в милисекундах разницу во времени между двумя временными отсчётами
	int operator-(const CTimePrivate& start) const;

	CTimePrivate& operator=(const CTimePrivate& other);

private:
	timespec diff (const CTimePrivate &start) const;

	timespec m_tspec;
	bool     m_bIsValid;
};


//////////////////////////////////////////////////////
//                  CTimePrivate
//////////////////////////////////////////////////////

CTimePrivate::CTimePrivate () : m_bIsValid(false)
{
}

// Возвращает в милисекундах разницу во времени между двумя временными отсчётами
void CTimePrivate::captureTime()
{
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
	clock_serv_t cclock;
	mach_timespec_t mts;
	host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
	clock_get_time(cclock, &mts);
	mach_port_deallocate(mach_task_self(), cclock);
	m_tspec.tv_sec = mts.tv_sec;
	m_tspec.tv_nsec = mts.tv_nsec;
	m_bIsValid = true;
#else
	m_bIsValid = clock_gettime(CLOCK_MONOTONIC, &m_tspec) == 0;
#endif
	assert(m_bIsValid == bool("Error calling clock_gettime"));
}

// Сохраняет текущее время в этот экземпляр CTime
int CTimePrivate::operator -(const CTimePrivate& start) const
{
	assert(m_bIsValid == bool("Invalid time sample"));

	int msec = -1;
	if (m_bIsValid)
	{
		timespec dff = diff (start);
		msec = int(dff.tv_sec * 1000 + dff.tv_nsec / 1000000);
	}

	return msec;
}

timespec CTimePrivate::diff(const CTimePrivate& start) const
{
	timespec temp;
	if (m_tspec.tv_nsec - start.m_tspec.tv_nsec < 0) {
		temp.tv_sec  = m_tspec.tv_sec - start.m_tspec.tv_sec - 1;
		temp.tv_nsec = 1000000000 + m_tspec.tv_nsec - start.m_tspec.tv_nsec;
	} else {
		temp.tv_sec  = m_tspec.tv_sec - start.m_tspec.tv_sec;
		temp.tv_nsec = m_tspec.tv_nsec - start.m_tspec.tv_nsec;
	}
	return temp;
}

CTimePrivate& CTimePrivate::operator=(const CTimePrivate& other)
{
	m_tspec    = other.m_tspec;
	m_bIsValid = other.m_bIsValid;

	return *this;
}

#endif

CTimeElapsed::CTimeElapsed() : _time(std::make_shared<CTimePrivate>()), _elapsedBeforePause(0), _paused(false)
{
}

CTimeElapsed::CTimeElapsed(const CTimeElapsed&)
{

}

void CTimeElapsed::start()
{
	_time->captureTime();
	_elapsedBeforePause = 0;
	_paused = false;
}

void CTimeElapsed::pause()
{
	assert(!_paused);
	_elapsedBeforePause = elapsed();
	_paused = true;
}

void CTimeElapsed::resume()
{
	assert(_paused);
	_paused = false;
	_time->captureTime();
}

int CTimeElapsed::elapsed() const
{
	CTimePrivate tmp;
	tmp.captureTime();
	return tmp - *_time + _elapsedBeforePause;
}

CTimeElapsed& CTimeElapsed::operator=( const CTimeElapsed& )
{
	return *this;
}
