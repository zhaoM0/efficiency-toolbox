#ifndef _TPOOL_H_
#define _TPOOL_H_

#include <pthread.h>

typedef struct Task_t {
    void (*entry)(void *arg);
    void *arg;
} Task;

typedef struct ThreadPool_t {
    Task *taskQ;                // task queue
    int   qCapacity;            // queue capacity
    int   qSize;                // queue size
    int   qFront;               // head pointer of taskQ
    int   qRear;                // tail pointer of taskQ

    pthread_t  managerID;       // manageer thread
    pthread_t *threadIDs;       // threads pool

    int minNum;                 // minimum num of threads
    int maxNum;                 // maximum num of threads
    int liveNum;                // active threads
    int busyNum;                // working threads
    int exitNum;                // destroyed threads
    int shutdown;               // thread pool status 

    pthread_mutex_t mutexPool;  // lock of thread pool
    pthread_mutex_t mutexBusy;  //
    pthread_cond_t  notFull;    // task queue isn't full
    pthread_cond_t  notEmpty;   // task queue isn't empty

} ThreadPool;


/* Thread Pooll API */
ThreadPool* tp_create(int min, int max, int qSize);

int tp_destroy(ThreadPool* pool);

int tp_add(ThreadPool* pool, void(*func)(void*), void* arg);

int tp_busy(ThreadPool* pool);

int tp_alive(ThreadPool* pool);

void* worker(void* arg);

void* manager(void* arg);

void  thread_exit(ThreadPool* pool);

#endif /* end of "tpool.h" */