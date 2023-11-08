#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef TIMER_H
#define TIMER_H
class Timer{
	public:
		/*countdown duration in miliseconds*/
		int duration;
		/*zero to stop/pause the timer*/
		struct itimerval zero_val;
		/*old value to record current time when stopped*/
		struct itimerval old_val;
		/*constant value that the timer needs to run*/
		struct itimerval const_val;

		Timer(int duration);
		void start();
		void pause();
		void resume();
		void stop();
};

#endif