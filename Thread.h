#include <pthread.h>
#include <list>
#include <csetjmp>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include <signal.h>
using namespace std;

typedef struct {
	/* pthread_t usually typedef as unsigned long int */
	pthread_t id;
	/* jmp_buf usually defined as struct with __jmpbuf internal buffer
	   which holds the 6 registers for saving and restoring state */
	jmp_buf jb;
	/* stack pointer for thread; for main thread, this will be NULL */
	char *stack;

	//change
	//bool isBlocked = false;
	int status;
	sem_t real_sem;
	//end change
} tcb_t;

//
typedef struct{
	int sem_value;
	list<tcb_t> *wait_queue;
	bool isInit = false;
} semaphore;

typedef struct{
	pthread_t id;
	void* return_value;
} dead_tcb_t;

/*function declarations*/
static void init();
static long int i64_ptr_mangle(long int p);
tcb_t* find_thread(pthread_t thread)
pthread_t pthread_self(void)
int pthread_create(pthread_t *restrict_thread, const pthread_attr_t 
*restrict_attr, void *(*start_routine)(void*), void *restrict_arg);
void pthread_exit(void* val);
void signal_handler(int signo);
void the_nowhere_zone(void);

//semaphore functions:
int sem_init(sem_t *sem, int pshared, unsigned value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);

//sync functions
void lock();
void unlock();
int pthread_join(pthread_t thread, void**value_ptr);
void pthread_exit_wrapper();

