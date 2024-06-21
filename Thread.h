#include <pthread.h>
#include <list>
#include <csetjmp>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include <signal.h>
using namespace std;
struct TCB{
    /*1. thread id*/
    pthread_t id;
    /*2. status*/
    int status;
    /*3. stack to store thread variables*/
    char* stack;
    /*4. stores the information of a calling environment to be restored 
    later.*/
    jmp_buf jumpBuffer;
};
/*function declarations*/
static void init();
static void scheduler(int sig);
static long int i64_ptr_mangle(long int p);
static TCB find_available(list<TCB>& thread_pool);
static void destroy_deadThreads(list<TCB>& dead_pool);
int pthread_create(pthread_t *restrict_thread, const pthread_attr_t 
*restrict_attr, void *(*start_routine)(void*), void *restrict_arg);
void pthread_exit(void* val);

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

