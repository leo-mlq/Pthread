#include "Timer.h"
Timer::Timer(int duration){
    this->duration=duration;
    /* set timer in seconds */
    this->const_val.it_value.tv_usec=this->duration/1000;
    /* set timer in microseconds */
    this->const_val.it_value.tv_usec=(this->duration*1000)%1000000;
    /*timer reset to duration when it is off, 0 means one shot timer*/
    this->const_val.it_interval=this->const_val.it_value;
}
void Timer::start(){
    setitimer(ITIMER_REAL, &(this->const_val), NULL);
}
void Timer::pause(){
    setitimer(ITIMER_REAL, &(this->zero_val), &(this->old_val));
}
void Timer::resume(){
    setitimer(ITIMER_REAL, &(this->old_val), NULL);
}
void Timer::stop(){
    setitimer(ITIMER_REAL, &(this->zero_val), NULL);
}
