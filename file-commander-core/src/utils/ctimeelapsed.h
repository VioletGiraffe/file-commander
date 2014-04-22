#ifndef CTIMEELAPSED_H
#define CTIMEELAPSED_H

#include <memory>

class CTimePrivate;
class CTimeElapsed
{
	CTimeElapsed(const CTimeElapsed&);
	CTimeElapsed& operator= (const CTimeElapsed&);

public:
	CTimeElapsed();

	void     start();
	void     pause();
	void     resume();
	int      elapsed() const;

private:
	std::shared_ptr<CTimePrivate> _time;
	int                           _elapsedBeforePause;
	bool                          _paused;

};

#endif // CTIMEELAPSED_H
