#include "Timer.h"
#include "thread.h"
using namespace std;
#define RUNNING 0
#define READY 1
#define BLOCKED 2
#define TERMINATED 3
#define STACK_SIZE 32764
//jmp_buf pointers
#define JB_BX 0
#define JB_SI 1
#define JB_DI 2
#define JB_BP 3
#define JB_SP 4 //stack pointer
#define JB_PC 5 //program counter
#define INTERVAL 50
using namespace std;
static pthread_t pid=0;
//takes care of running, ready and blocked
static list<TCB> thread_pool;
//save them here and let the main thread do the cleaning job
static list<TCB> dead_pool;
static bool isInitialized = false;
static Timer* timer;
static struct sigaction act;
/*function implementation*/
static void init(){
    isInitialized = true;
    timer = new Timer(INTERVAL);
    /*create main() thread*/
    TCB main_thread;
    main_thread.id=pid++;
    main_thread.status=READY;
    //main() does not do anything except initializing and waiting for 
    all threads to finish
    main_thread.stack=NULL;
    //clear the jumpBuffer
    memset(&main_thread.jumpBuffer,0, sizeof(main_thread.jumpBuffer));
    //empty the signal mask inside jumpBuffer to prevent intervention
    sigemptyset(&main_thread.jumpBuffer->__saved_mask);
    thread_pool.push_back(main_thread);
    /*set up signal*/
    act.sa_handler = scheduler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NODEFER;
    if(sigaction(SIGALRM, &act, NULL) == -1) {
    perror("Unable to catch SIGALRM");
    exit(1);
    }
    /*start the timer for scheduling*/
    timer->start();
    /*wait for the timer to go off and switch to creating another 
    thread*/
    pause();
}
static TCB find_available(list<TCB>& thread_pool){
    TCB next_available = thread_pool.front();
    /*if main is the only one in the pool, schedule it to clean up*/
    if(thread_pool.size()!=1){
        while(next_available.status!=READY){
            thread_pool.pop_front();
            thread_pool.push_back(next_available);
            next_available=thread_pool.front();
            }
        }
        return next_available;
    }
    static void destroy_deadThreads(list<TCB>& dead_pool){
    while(dead_pool.size()>0){
        TCB dead_thread = dead_pool.back();
        dead_pool.pop_back();
        free(dead_thread.stack);
        dead_thread.stack=NULL;
        }
    }
    int pthread_create(pthread_t *restrict_thread, const pthread_attr_t 
    *restrict_attr, void *(*start_routine)(void*), void *restrict_arg) {
    if(!isInitialized) init();
    /*pause the timer to create a new thread*/
    //should be atomic action
    timer->pause();
    /*create a new thread*/
    TCB new_thread;
    new_thread.id=pid++;
    *restrict_thread=pid;
    new_thread.status=READY;
    new_thread.stack = (char*)malloc(STACK_SIZE);
    //clear the stack
    memset(new_thread.stack,0, STACK_SIZE);
    //clear the jumpBuffer
    memset(&new_thread.jumpBuffer, 0, sizeof(new_thread.jumpBuffer));
    //empty the signal mask inside jumpBuffer to prevent intervention
    sigemptyset(&new_thread.jumpBuffer->__saved_mask);
    /**/
    //pointer arithematic is not allowed on void
    //esp points to top address of the queue
    int* esp=(int*)(new_thread.stack+STACK_SIZE);
    /*en-stack the parameter and return pointer for start_routine*/
    //the parameter must be on bottom of stack bc start_routine needs 
    it and then returns pthread_exit after execution 
    //a int point takes 4 bytes;
    //esp[0] = *esp = bottom of stack = base of stack + STACK_SIZE
    //esp[-1] = *(esp-1) = bottom of stack - 4
    esp[-1]=(intptr_t)restrict_arg;
    /*en-stack the pthread_exit*/
    esp[-2]=(intptr_t)pthread_exit;
    esp-=2;
    /*let them jumpBuffer links to the thread's stack and 
    start_routine*/
    //link to stack, store the stack pointer as value in the jmp_buf
    new_thread.jumpBuffer->__jmpbuf[JB_SP]=i64_ptr_mangle((intptr_t)esp
    );
    //link the start_routine to JB_PC so that jumpBuffer knows what to 
    execute
    new_thread.jumpBuffer->__jmpbuf[JB_PC]=i64_ptr_mangle((intptr_t)sta
    rt_routine);
    /*push the ready thread into thread_pool*/
    thread_pool.push_back(new_thread);
    /*set a jump point for first round jump back*/
    setjmp(new_thread.jumpBuffer);
    /*resume the timer*/
    timer->resume();
    return 0;
}
void pthread_exit(void* val){
    /*pause timer, should be atomic*/
    timer->stop();
    /*get the current thread*/
    TCB cur_thread = thread_pool.front();
    /*main thread exiting*/
    //main has to explictly call pthread_exit while others return here 
    after start_routine finishes
    if(cur_thread.id==0){
    /*still threads in the pool other than main_thread*/
        if(thread_pool.size()>1){
        /*if main_thread calls pthread_exit before others finish*/
        //clean up will not perform bc last thread long jump back to main_thread but it has quited 
        //timer signal also not handled.
        int ret_setjump = setjmp(cur_thread.jumpBuffer);
            if(ret_setjump!=0) {
            /*all threads have exited*/
            //clean up
            timer->stop();
            destroy_deadThreads(dead_pool);
            /*exit to let the program clean up main_thread*/
            exit(0);
            }
            thread_pool.pop_front();
            //main is now blocked;
            cur_thread.status=BLOCKED;
            thread_pool.push_back(cur_thread);
            /*find the next ready thread*/
            TCB next_available = find_available(thread_pool);
            //for example, if main_thread already terminated, we do not 
            want to schedule it again
            //otherwise, it will jump to the clean up point;
            /*run next available*/
            next_available.status=RUNNING;
            timer->start();
            longjmp(next_available.jumpBuffer, cur_thread.id+1);
        }
        else{
        /*if main calls pthread_exit at last, just go ahead 
        cleanup*/
        timer->stop();
        destroy_deadThreads(dead_pool);
        /*exit to let the program clean up main_thread*/
        exit(0);
        }
    }
    else{
        thread_pool.pop_front();
        cur_thread.status=TERMINATED;
        dead_pool.push_back(cur_thread);
        /*find the next ready thread*/
        TCB next_available = find_available(thread_pool);
        //for example, if main_thread already terminated, we do not 
        want to schedule it again
        //otherwise, it will jump to the clean up point;
        /*run next available*/
        next_available.status=RUNNING;
        timer->start();
        longjmp(next_available.jumpBuffer, cur_thread.id+1);
    }
}
static void scheduler(int sig){
    /*pause the timer during context switching which should be atomic*/
    timer->pause();
    /*return if no thread or only main thread*/
    if(thread_pool.size()<=1){
        timer->resume();
        return;
    }
    /*get the current thread*/
    //for first round, the first cur_thread must be main_thread (first in first to run)
    //no first round jump point was set for main_thread
    TCB cur_thread = thread_pool.front();
    thread_pool.pop_front();
    /*save the states of current thread in jumpBuffer*/
    int ret_setjump = setjmp(cur_thread.jumpBuffer);
    if(ret_setjump==0){
        cur_thread.status=READY;
        thread_pool.push_back(cur_thread);
        /*find the next ready thread*/
        TCB next_available = find_available(thread_pool);
        //for example, if main_thread already terminated, we do not 
        want to schedule it again
        //otherwise, it will jump to the clean up point;
        /*run next available*/
        next_available.status=RUNNING;
        timer->resume();
        /*jump to the previuos saved state*/
        //first round jump point is set when thread was created.
        //later rounds will jump back to point set in the scheduler.
        longjmp(next_available.jumpBuffer, cur_thread.id+1);
    }
    return;
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