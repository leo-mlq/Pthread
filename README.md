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

![Thread Stack Structure](image_link)

When creating the thread, the parameter and return instruction pointer of `start_routine` must be pushed into the stack. To achieve this operation on the thread stack, an ESP pointer is first initialized in int type to point to the bottom of the stack. Because the parameter (`restrict_arg`) is a void pointer which takes 4 bytes to store, the ESP pointer decrements by 1 to accommodate enough space. After finishing execution of `start_routine`, the thread should call `pthread_exit()` itself to finish its lifecycle; thus, the return instruction pointer should be set to the address of `pthread_exit()`. Similarly, ESP decrements by 1 to store the return address. Both `restrict_arg` and `pthread_exit()` are void pointers and needed to cast to int pointer in order to be stored in the stack. Below codes show the operation.

```c
int* esp=(int*)(new_thread.stack+STACK_SIZE);
esp[-1]=(intptr_t)restrict_arg;
esp[-2]=(intptr_t)pthread_exit;
esp-=2;
