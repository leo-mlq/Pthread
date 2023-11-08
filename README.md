# Pthread
A simple C++ simulation of posix thread library

# Simulation of POSIX Threads

## 3.1 Introduction

In this section, a simulation of a basic POSIX thread system is implemented for Linux. The system has the ability to create new threads, execute threads independently, and terminate them. Each thread is able to save its context while switching to another thread by scheduling policy. It consists of two main functions, `pthread_create()` and `pthread_exit()`, along with a Round Robin scheduler and Thread Control Block data structure. A timer is also implemented to carry out Round Robin scheduling policy which gives each thread a certain duration (in milliseconds) to execute. However, `pthread_join()` is not implemented in this system whereas the `main()` thread will exit after all threads have exited.

## 3.2 Thread Control Block (TCB)

A TCB consists of the following attributes:
- `pthread_t id`
- `int status`
- `char* stack`
- `jmp_buf jumpBuffer`

An `id` is a unique identification of a thread whose status could be running, ready, blocked, or terminated. In order to save its local variables, function parameters, and return instruction pointers, a thread is given a stack of 32764 bytes. Lastly, a `jmp_buf` type buffer is used to save and retrieve context switching information. In particular, `setjmp()` saves the stack pointer and instruction pointer into `jumpBuffer` whereas `longjmp()` retrieves these pointers and restores the saved register values for resumption of execution.

## 3.3 `pthread_create()`

Prior to a thread is created or the first time when `pthread_create()`, initiation is needed to be done; It includes creating a list to store alive (ready, blocked, and running) threads and a list to store dead threads, set up timer and signal for scheduling, and initializing `main()` thread. In a way, `main()` thread represents the `main()` function, which does not need to perform `start_routine`; thus, it does not require the stack to be allocated and jumpBuffer set-up. However, in this system, `main()` thread is responsible for clean-up after all threads have finished execution, by explicitly calling `pthread_exit()`. After creating `main()` thread and pushing it into the alive thread pool, it is paused for the timer to go off. After 50ms, the scheduler kicks in and returns immediately because `main()` thread is the only one in the alive thread pool and it has no duties.

Now, a new thread can be created. First, the timer should be paused because `pthread_create()` is an atomic action. Then, a TCB is initialized, and its stack is allocated. A stack is simply a char array which is used to store parameters and return address. As shown below, the thread stack is a segment of consecutive memory; it has a base address as the bottom of the stack and a top of stack with a higher value address. An ESP pointer points to the top of the stack and moves to higher address as the stack pops while moving to lower addresses as the stack pushes.

![Thread Stack Structure](https://info.varonis.com/hubfs/Imported_Blog_Media/stack-memory-architecture-1-981x1024.png)

When creating the thread, the parameter and return instruction pointer of `start_routine` must be pushed into the stack. To achieve this operation on the thread stack, an ESP pointer is first initialized in int type to point to the bottom of the stack. Because the parameter (`restrict_arg`) is a void pointer which takes 4 bytes to store, the ESP pointer decrements by 1 to accommodate enough space. After finishing execution of `start_routine`, the thread should call `pthread_exit()` itself to finish its lifecycle; thus, the return instruction pointer should be set to the address of `pthread_exit()`. Similarly, ESP decrements by 1 to store the return address. Both `restrict_arg` and `pthread_exit()` are void pointers and needed to cast to int pointer in order to be stored in the stack. Below codes show the operation.

```c
int* esp=(int*)(new_thread.stack+STACK_SIZE);
esp[-1]=(intptr_t)restrict_arg;
esp[-2]=(intptr_t)pthread_exit;
esp-=2;

Although it is loaded with parameter and return address, it does not automatically pop or execute. To correct this, the current state of a thread is saved into the `jumpBuffer` which is `jmp_buf` type defined as an array of 8 long integers.

1. `JB_RBX`
2. `JB_RBP`
3. `JB_R12`
4. `JB_R13`
5. `JB_R14`
6. `JB_R15`
7. `JB_RSP`
8. `JB_PC`

These 8 integers are specified to store different types of pointers. In this system, only `JB_RSP` and `JB_PC` are used; the first stores the thread stack ESP pointer, and the latter one stores the address of `start_routine` when `setjmp()` is called. As long as `longjmp()` is called, these pointers are loaded/restored to the process’s execution environment. Then, the process continues at the point in the program corresponding to the `setjmp()` call. Below codes show the operation; the `i64_ptr_mangle` is intended to encrypt pointers saved in `jmp_buf` for security reasons in the Linux Operating System.

```c
new_thread.jumpBuffer->__jmpbuf[JB_SP] = i64_ptr_mangle((intptr_t)esp);
new_thread.jumpBuffer->__jmpbuf[JB_PC] = i64_ptr_mangle((intptr_t)start_routine);


## 3.4 Timer, Scheduling Policy, and Context Switching

The system employs a simple Round Robin scheduling policy, where each thread is given an equal duration of time to execute. In addition, thread preemption is implemented to prevent each thread from executing until completion. To achieve Round Robin, `SIGALRM` and `sigaction()` are cooperatively used. In a sentence, an alarm signal is fired every period and is caught by `sigaction()`, which invokes a handling function to perform thread switching. Ensuring the alarm signal is generated precisely, the Timer class utilizes `setitimer()` function with `ITIMER_REAL` as the interval timer type. It has 4 member functions: `start()`, `pause()`, `resume()`, and `stop()`. When it starts, the interval timer value decrements from a preset value in real-time. The `SIGALRM` signal is generated when the timer counts down to 0. Another advantage of `setitimer()` is the simplicity of setting up a periodic timer by assigning `it_interval` the same value as `it_value`. `sigaction()` is selected over `signal()` to handle the alarm signal and perform scheduling; it allows users to specify a set of flags that modify the behavior of the signal. Since the alarm signal is scheduled to deliver periodically, we do not expect it to be blocked after the first arrival. Therefore, a `SA_NODEFER` flag is used. Below codes demonstrate `setitimer()` and `sigaction()`.

```c
const_val.it_value.tv_usec = duration / 1000;
const_val.it_value.tv_usec = (duration * 1000) % 1000000;
/*timer reset to duration when it is off, 0 means one-shot timer*/
const_val.it_interval = const_val.it_value;
/*set up signal*/
act.sa_handler = scheduler;
sigemptyset(&act.sa_mask);
act.sa_flags = SA_NODEFER;
if(sigaction(SIGALRM, &act, NULL) == -1) {
    perror("Unable to catch SIGALRM");
    exit(1);
}


The scheduler above defines how threads are being switched when the timer is up. The first step is to pause the timer. If the `main()` thread is the only one in the alive thread pool, the scheduler resumes the timer and returns control to the `main()` thread. If not, it changes the current running thread to the status of ready, removes it from the thread pool, and saves the current state into `jumpBuffer`. Then, a next available thread is selected from the thread pool and scheduled to run by loading its saved state. Timer resumes as well.

## 3.5 `pthread_exit()`

There are two scenarios where `pthread_exit()` is called: `main()` calls it and a thread calls. When `main()` calls `pthread_exit()`, we first check if there are still alive threads waiting to finish execution. If yes, we set up a jump-back point for the `main()` thread to clean up and mark it as blocked, which results in being skipped by the scheduler when the timer goes off. If no, we just go ahead to clean up and free the thread stack.

Under the circumstance when `pthread_exit()` is called and the calling object is not `main()`, a thread has finished executing `start_route` and automatically returns to `pthread_exit()`. Its status is changed to terminated and is removed from the alive thread pool and placed into the dead thread pool. The dead thread pool will be emptied by the `main()` thread after every thread has finished and jumping back to the point we set above. Then, a next available thread is manually scheduled to run.
