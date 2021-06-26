#include "timer.h"

#include <sys/time.h>


namespace utils
{


long long Timer::getTime()
{
	struct timeval tv;
	long long value;

	gettimeofday(&tv, NULL);
	value = (long long) tv.tv_sec * 1000000 + tv.tv_usec;
	return value;
}


long long Timer::getValue() const
{
	// Timer is stopped
	if (state == StateStopped)
		return total_time;

	// Timer is running
	long long ellapsed = getTime() - start_time;
	return total_time + ellapsed;
}


void Timer::Start()
{
	// Timer already running
	if (state == StateRunning)
		return;
	
	// Start timer
	state = StateRunning;
	start_time = getTime();
}


void Timer::Stop()
{
	// Timer already stopped
	if (state == StateStopped)
		return;
	
	// Stop timer
	long long ellapsed = getTime() - start_time;
	state = StateStopped;
	total_time += ellapsed;
}


void Timer::Reset()
{
	total_time = 0;
	start_time = getTime();
}


}  // namespace utils

