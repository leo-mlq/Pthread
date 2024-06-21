#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <queue>
#include <list>
#include <semaphore.h>
#include <iostream>
#include <utility>




/*
 *Timer globals
 */
static struct timeval tv1,tv2;
static struct itimerval interval_timer = {0}, current_timer = {0}, zero_timer = {0};
static struct sigaction act;



/*
 * Timer macros for more precise time control
 */

#define PAUSE_TIMER setitimer(ITIMER_REAL,&zero_timer,&current_timer)
#define RESUME_TIMER setitimer(ITIMER_REAL,&current_timer,NULL)
#define START_TIMER current_timer = interval_timer; setitimer(ITIMER_REAL,&current_timer,NULL)
#define STOP_TIMER setitimer(ITIMER_REAL,&zero_timer,NULL)
/* number of ms for timer */
#define INTERVAL 50
//max value for semaphores
#define SEM_VALUE_MAX 65536
#define RUNNING 1
#define READY 2
#define BLOCKED 3
#define EXITED 4





/* queue for pool thread, easy for round robin */
//change to list, traverse the list
static std::list<tcb_t> thread_pool;
//add dead thread pool, pair<pthread_id,return value>
static std::vector<dead_tcb_t> dead_thread_pool;
/* keep separate handle for main thread */
static tcb_t main_tcb;
static tcb_t garbage_collector;

/* for assigning id to threads; main implicitly has 0 */
static unsigned long id_counter = 1;
/* we initialize in pthread_create only once */
static int has_initialized = 0;

bool ready2collect = false;




/*
 * init()
 *
 * Initialize thread subsystem and scheduler
 * only called once, when first initializing timer/thread subsystem, etc...
 */
void init() {
	/* on signal, call signal_handler function */
	act.sa_handler = signal_handler;
	/* set necessary signal flags; in our case, we want to make sure that we intercept
	   signals even when we're inside the signal_handler function (again, see man page(s)) */
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NODEFER;

	/* register sigaction when SIGALRM signal comes in; shouldn't fail, but just in case
	   we'll catch the error  */
	if(sigaction(SIGALRM, &act, NULL) == -1) {
		perror("Unable to catch SIGALRM");
		exit(1);
	}

	/* set timer in seconds */
	interval_timer.it_value.tv_sec = INTERVAL/1000;
	/* set timer in microseconds */
	interval_timer.it_value.tv_usec = (INTERVAL*1000) % 1000000;
	/* next timer should use the same time interval */
	interval_timer.it_interval = interval_timer.it_value;

	/* create thread control buffer for main thread, set as current active tcb */
	main_tcb.id = 0;
	main_tcb.stack = NULL;
	//change
	main_tcb.status = READY;
	sem_init(&(main_tcb.real_sem),0,0);
	//end change

	/* front of thread_pool is the active thread */
	thread_pool.push_back(main_tcb);

	/* set up garbage collector */
	garbage_collector.id = 128;
	garbage_collector.stack = (char *) malloc (32767);

	/* initialize jump buf structure to be 0, just in case there's garbage */
	memset(&garbage_collector.jb,0,sizeof(garbage_collector.jb));
	/* the jmp buffer has a stored signal mask; zero it out just in case */
	sigemptyset(&garbage_collector.jb->__saved_mask);

	/* garbage collector 'lives' in the_nowhere_zone */
	garbage_collector.jb->__jmpbuf[4] = ptr_mangle((uintptr_t)(garbage_collector.stack+32759));
	garbage_collector.jb->__jmpbuf[5] = ptr_mangle((uintptr_t)the_nowhere_zone);

	/* Initialize timer and wait for first sigalarm to go off */
	START_TIMER;
	pause();
}



/*
 * pthread_create()
 *
 * create a new thread and return 0 if successful.
 * also initializes thread subsystem & scheduler on
 * first invocation
 */
int pthread_create(pthread_t *restrict_thread, const pthread_attr_t *restrict_attr,
                   void *(*start_routine)(void*), void *restrict_arg) {

	/* set up thread subsystem and timer */
	if(!has_initialized) {
		has_initialized = 1;
		init();
	}

	/* pause timer while creating thread */
    PAUSE_TIMER;

	/* create thread control block for new thread
	   restrict_thread is basically the thread id
	   which main will have access to */
	tcb_t tmp_tcb;
	tmp_tcb.id = id_counter++;
	//change
	tmp_tcb.status = READY;
	sem_init(&(tmp_tcb.real_sem),0,0);
	//end change
	*restrict_thread = tmp_tcb.id;

	/* simulate function call by pushing arguments and return address to the stack
	   remember the stack grows down, and that threads should implicitly return to
	   pthread_exit after done with start_routine */

	tmp_tcb.stack = (char *) malloc (32767);

	*(int*)(tmp_tcb.stack+32763) = (int)restrict_arg;
	*(int*)(tmp_tcb.stack+32759) = (int)pthread_exit_wrapper;

	/* initialize jump buf structure to be 0, just in case there's garbage */
	memset(&tmp_tcb.jb,0,sizeof(tmp_tcb.jb));
	/* the jmp buffer has a stored signal mask; zero it out just in case */
	sigemptyset(&tmp_tcb.jb->__saved_mask);

	/* modify the stack pointer and instruction pointer for this thread's
	   jmp buffer. don't forget to mangle! */
	tmp_tcb.jb->__jmpbuf[4] = ptr_mangle((uintptr_t)(tmp_tcb.stack+32759));
	tmp_tcb.jb->__jmpbuf[5] = ptr_mangle((uintptr_t)start_routine);

	/* new thread is ready to be scheduled! */
	thread_pool.push_back(tmp_tcb);

    /* resume timer */
    RESUME_TIMER;

    return 0;
}



/*
 * pthread_self()
 *
 * just return the current thread's id
 * undefined if thread has not yet been created
 * (e.g., main thread before setting up thread subsystem)
 */
pthread_t pthread_self(void) {
	if(thread_pool.size() == 0) {
		return 0;
	} else {
		return (pthread_t)thread_pool.front().id;
	}
}



/*
 * pthread_exit()
 *
 * pthread_exit gets returned to from start_routine
 * here, we should clean up thread (and exit if no more threads)
 */
void pthread_exit(void *value_ptr) {
	/* just exit if not yet initialized */
	if(has_initialized == 0) {
		exit(0);
	}

	/* stop the timer so we don't get interrupted */
	//change
	STOP_TIMER;
	thread_pool.front().status = EXITED;
	dead_tcb_t dead_tcb;
	dead_tcb.id = thread_pool.front().id;
	//std::cout<<"thread exit "<<dead_tcb.id<<std::endl;
	dead_tcb.return_value = value_ptr;
	dead_thread_pool.push_back(dead_tcb);
	sem_post(&(thread_pool.front().real_sem));
	//end change
	if(thread_pool.front().id == 0) {
		/* if its the main thread, still keep a reference to it
	       we'll longjmp here when all other threads are done */
		main_tcb = thread_pool.front();
		if(setjmp(main_tcb.jb)) {
			/* garbage collector's stack should be freed by OS upon exit;
			   We'll free anyways, for completeness */
			//std::cout<<"back to main thread "<<std::endl;
			free((void*) garbage_collector.stack);
			dead_thread_pool.clear();
			exit(0);
		}
	}

	/* Jump to garbage collector stack frame to free memory and scheduler another thread.
	   Since we're currently "living" on this thread's stack frame, deleting it while we're
	   on it would be undefined behavior */
	longjmp(garbage_collector.jb,1);
}


/*
 * signal_handler()
 *
 * called when SIGALRM goes off from timer
 */
void signal_handler(int signo) {

	/* if no other thread, just return */
	if(thread_pool.size() <= 1) {
		return;
	}

	/* Time to schedule another thread! Use setjmp to save this thread's context
	   on direct invocation, setjmp returns 0. if jumped to from longjmp, returns
	   non-zero value. */
	if(setjmp(thread_pool.front().jb) == 0) {
		/* switch threads */
		//change
		if(thread_pool.front().status!=BLOCKED){
			//std::cout<<"test1"<<thread_pool.front().id<<std::endl;
			thread_pool.front().status = READY;
			thread_pool.push_back(thread_pool.front());
			thread_pool.pop_front();
			while(thread_pool.front().status==BLOCKED){
				thread_pool.push_back(thread_pool.front());
				thread_pool.pop_front();
			}
			thread_pool.front().status = RUNNING;
		}
		else{
			int tmp = thread_pool.front().id;
			//std::cout<<"test2"<<thread_pool.front().id<<std::endl;
			while(thread_pool.front().status==BLOCKED){
				thread_pool.push_back(thread_pool.front());
				thread_pool.pop_front();
			}
			//thread_pool.front().status = READY;
			thread_pool.front().status = RUNNING;
		}
		//end change
		//change
		//std::cout<<thread_pool.front().id<<std::endl;
		//end change
		/* resume scheduler and GOOOOOOOOOO */
		//std::cout<<"from thread "<<tmp<<" jump to thread "<<thread_pool.front().id<<std::endl;
		longjmp(thread_pool.front().jb,1);
	}
	//std::cout<<"back to thread "<<thread_pool.front().id<<std::endl;
	/* resume execution after being longjmped to */
	return;
}


/*
 * the_nowhere_zone()
 *
 * used as a temporary holding space to safely clean up threads.
 * also acts as a pseudo-scheduler by scheduling the next thread manually
 */
void the_nowhere_zone(void) {
	//std::cout<<"test"<<std::endl;
	/* free stack memory of exiting thread
	   Note: if this is main thread, we're OK since
	   free(NULL) works */
	//std::cout<<"in the nowhere zone "<<thread_pool.front().id<<std::endl;
	free((void*) thread_pool.front().stack);
	thread_pool.front().stack = NULL;
	//change
	sem_destroy(&(thread_pool.front().real_sem));
	//end change
	/* Don't schedule the thread anymore */
	thread_pool.pop_front();

	/* If the last thread just exited, jump to main_tcb and exit.
	   Otherwise, start timer again and jump to next thread*/
	if(thread_pool.size() == 0) {
		//std::cout<<"from nowhere zone to main "<<main_tcb.id<<std::endl;
		longjmp(main_tcb.jb,1);
	} else {
		//change
		while(thread_pool.front().status==BLOCKED){
			thread_pool.push_back(thread_pool.front());
			thread_pool.pop_front();
		}
		//end change
		START_TIMER;
		//std::cout<<thread_pool.front().id<<std::endl;
		//std::cout<<"from nowhere zone to next non-blocked thread "<<thread_pool.front().id<<std::endl;
		longjmp(thread_pool.front().jb,1);
	}
}
//a helper function to find the target thread in thread_pool;
tcb_t* find_thread(pthread_t thread){
	tcb_t* tmp;
	for(std::list<tcb_t>::iterator it=thread_pool.begin();it!=thread_pool.end();++it){
		if((*it).id==thread){
			tmp = &(*it);
			return tmp;
		}
	}
	tmp = NULL;
	return tmp;

}
//pthread join, intialize semaphore before wait
int pthread_join(pthread_t thread, void**value_ptr){
	if(thread <= id_counter){
		lock();
	 	if(!find_thread(thread)){
			//std::cout<<"test1"<<std::endl;
			for(unsigned int i=0;i<dead_thread_pool.size();i++){
				if(dead_thread_pool[i].id == thread){
					if(dead_thread_pool[i].return_value!=NULL){
						*value_ptr = (dead_thread_pool[i].return_value);
					}
				}
			}
			unlock();
			return 0;
		 }
		else if(thread_pool.front().id==thread){
			unlock();
			return -1;
		}
		else{
			tcb_t* target_thread = find_thread(thread);
			//semaphore *tmp = (semaphore*)target_thread->real_sem.__align;
			//tmp->isInit = true;
			//tmp->wait_queue = new std::list<tcb_t>;
			//target_thread->real_sem.__align = (long int)tmp;
			//sem_init(&(target_thread->real_sem),0,0);
			unlock();
			sem_wait(&(target_thread->real_sem));

			for(unsigned int i=0;i<dead_thread_pool.size();i++){
				if(dead_thread_pool[i].id == thread){
					if(dead_thread_pool[i].return_value!=NULL){
						*value_ptr = (dead_thread_pool[i].return_value);
					}
					//std::cout<<"collected return value"<<std::endl;
				}
			}
			return 0;
		}
	}
	else
		return -1;
}


//lock() block signal
void lock(){
	sigset_t alarm_set;
	sigemptyset(&alarm_set);
	sigaddset(&alarm_set, SIGALRM);
	sigprocmask(SIG_BLOCK,&alarm_set,NULL);

}
//unlock() unlock signal
void unlock(){
	sigset_t alarm_set;
	sigemptyset(&alarm_set);
	sigaddset(&alarm_set, SIGALRM);
	sigprocmask(SIG_UNBLOCK,&alarm_set,NULL);
}

//semaphore initilize, return 0 for sucessfully init, otherwise 1 for undefined behavior 
int sem_init(sem_t *sem, int pshared, unsigned value){
	semaphore *sem_ptr = (semaphore*)malloc(sizeof(semaphore));
	if(value>SEM_VALUE_MAX) sem_ptr->sem_value = SEM_VALUE_MAX;
	else{
		sem_ptr->sem_value = value;
		sem_ptr->isInit = true;
		sem_ptr->wait_queue = new std::list<tcb_t>;
		sem->__align = (long int)sem_ptr;
	}
	return 0;
}
//return 0 for sucessfully destroy, otherwise -1 for undefined behavior 
int sem_destroy(sem_t *sem){
	semaphore *tmp = (semaphore*)sem->__align;
	if(!tmp->isInit){
		free(tmp); 
		return -1;
	}
	else{
		delete(tmp->wait_queue);
		free(tmp);
		return 0;
	}
}
//return 0 for success,otherwise -1;
int sem_wait(sem_t *sem){
	lock();
	semaphore *tmp = (semaphore*)(sem->__align);
	//std::cout<<"test"<<std::endl;
	//if value > 0, decrement and return;
	if(!tmp->isInit){
		//std::cout<<"test1"<<std::endl;
		return -1;
	}
	if(tmp->sem_value>0){

		//std::cout<<"test2"<<std::endl;
		tmp->sem_value--;
		unlock();
		return 0;
	}
	//if = 0,wait
	else{
		//std::cout<<"test3"<<std::endl;
		thread_pool.front().status = BLOCKED;

		//std::cout<<"blocking "<<thread_pool.front().id<<std::endl;
		tmp->wait_queue->push_back(thread_pool.front());
		unlock();
		//wait for the scheduler to come in
		pause();
		return 0;
	}
	return -1;
}

//return 0 for success,otherwise -1;
int sem_post(sem_t *sem){
	semaphore *tmp = (semaphore*)sem->__align;
	if(!tmp->isInit){ 
		return -1;
	}
	else{
		lock();
		tmp->sem_value++;
		if(tmp->sem_value>0&&(tmp->wait_queue->size()>0)){
			tmp->sem_value = 0;
			pthread_t woken_up_id = tmp->wait_queue->front().id;
			tmp->wait_queue->pop_front();
			tcb_t* woken_up_thread = find_thread(woken_up_id);
			woken_up_thread->status = READY;
			//std::cout<<"block thread "<<woken_up_id<<" are being unblocked"<<std::endl;
			unlock();
			return 0;
		}
		else{
			unlock();
			return -1;
		}
	}
}

/*modify pthread_exit to get return value*/
void pthread_exit_wrapper()
{
  unsigned int res;
  asm("movl %%eax, %0\n":"=r"(res));
  pthread_exit((void *) res);
}

static long int i64_ptr_mangle(long int p)
{
    unsigned int ret;
    __asm__(" movl %1, %%eax;\n"
    " xorl %%gs:0x18, %%eax;"
    " roll $0x9, %%eax;"
    " movl %%eax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%eax"
    );
    return ret;
}
