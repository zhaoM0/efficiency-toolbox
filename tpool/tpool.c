#include "tpool.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define INC_NUM 3

/* Thread Pooll API */
ThreadPool* tp_create(int min, int max, int qSize) {
    ThreadPool *tpool = (ThreadPool *)malloc(sizeof(ThreadPool));    
    if(tpool == NULL) {
        printf("thread pool malloc fail..\n");
        goto FAIL;
    }

    tpool->threadIDs = (pthread_t *)malloc(sizeof(pthread_t) * max);
    if(tpool->threadIDs == NULL) {
        printf("threadIDs malloc fail..\n");
        goto FAIL;
    }
    memset(tpool->threadIDs, 0, sizeof(pthread_t) * max);

    tpool->minNum  = min;
    tpool->maxNum  = max;
    tpool->liveNum = min;
    tpool->busyNum = 0;
    tpool->exitNum = 0; 

    if (pthread_mutex_init(&tpool->mutexPool, NULL) != 0 || 
        pthread_mutex_init(&tpool->mutexBusy, NULL) != 0 ||
        pthread_cond_init(&tpool->notEmpty, NULL) != 0 ||
        pthread_cond_init(&tpool->notFull, NULL) != 0) 
    {
        printf("mutex or condition init fail..\n");
        goto FAIL;
    }

    // init task queue 
    tpool->taskQ = (Task *)malloc(sizeof(Task) * qSize);
    tpool->qCapacity = qSize;
    tpool->qSize  = 0;
    tpool->qFront = 0;
    tpool->qRear  = 0;

    tpool->shutdown = 0;

    pthread_create(&tpool->managerID, NULL, manager, tpool);
    for(int i = 0; i < min; ++i) {
        pthread_create(&tpool->threadIDs[i], NULL, worker, tpool);
    }

    return tpool;

FAIL:
    if (tpool && tpool->threadIDs) free(tpool->threadIDs);
    if (tpool && tpool->taskQ)     free(tpool->taskQ);
    if (tpool)                     free(tpool);

    return NULL;
}

int tp_destroy(ThreadPool* tpool) {
    if (tpool == NULL) return -1;

    tpool->shutdown = 1;
    pthread_join(tpool->managerID, NULL);

    // signal consumer thread
    for (int i = 0; i < tpool->liveNum; ++i) {
        pthread_cond_signal(&tpool->notEmpty);
    }

    if(tpool->taskQ)     free(tpool->taskQ);
    if(tpool->threadIDs) free(tpool->threadIDs);

    pthread_mutex_destroy(&tpool->mutexBusy);
    pthread_mutex_destroy(&tpool->mutexPool);
    pthread_cond_destroy(&tpool->notEmpty);
    pthread_cond_destroy(&tpool->notFull);

    free(tpool);
    return 0;
}

int tp_add(ThreadPool* tpool, void(*func)(void*), void* arg) {
    pthread_mutex_lock(&tpool->mutexPool);
    
    if (tpool->shutdown) {
        pthread_mutex_unlock(&tpool->mutexPool);
        return -1;
    }

    assert(tpool->shutdown == 0);
    while(tpool->qSize == tpool->qCapacity)
        pthread_cond_wait(&tpool->notFull, &tpool->mutexPool);

    tpool->taskQ[tpool->qRear].entry = func;
    tpool->taskQ[tpool->qRear].arg = arg;
    tpool->qRear = (tpool->qRear + 1) % tpool->qCapacity;
    tpool->qSize += 1;

    pthread_cond_signal(&tpool->notEmpty);
    pthread_mutex_unlock(&tpool->mutexPool);
    return 0;
}

int tp_busy(ThreadPool* tpool) {
    return tpool->busyNum;
}

int tp_alive(ThreadPool* tpool) {
    return tpool->liveNum;
}


void* worker(void* arg) {
    ThreadPool* tpool = (ThreadPool*)arg;

    while(1) {
        pthread_mutex_lock(&tpool->mutexPool);
        // check if there are threads pending deletion
        if (tpool->exitNum > 0 && tpool->liveNum > tpool->minNum) {
            tpool->exitNum -= 1;
            tpool->liveNum -= 1;
            pthread_mutex_unlock(&tpool->mutexPool);
            thread_exit(tpool);
            assert(0);      // defensive wall
        }

        // task queue is or not empty
        while(tpool->qSize == 0 && !tpool->shutdown) {
            pthread_cond_wait(&tpool->notEmpty, &tpool->mutexPool);
        }
        assert(tpool->qSize > 0);

        if (tpool->shutdown) {
            pthread_mutex_unlock(&tpool->mutexPool);
            thread_exit(tpool);
        }

        // fetch a task from task queue
        Task task;
        task.entry = tpool->taskQ[tpool->qFront].entry;
        task.arg = tpool->taskQ[tpool->qFront].arg;

        tpool->qFront = (tpool->qFront + 1) % tpool->qCapacity;
        tpool->qSize -= 1;

        // signal
        pthread_cond_signal(&tpool->notFull);
        pthread_mutex_unlock(&tpool->mutexPool);

        // start new task
        printf("tid %ld start working ..\n", pthread_self());
        pthread_mutex_lock(&tpool->mutexBusy);
        tpool->busyNum += 1;
        pthread_mutex_unlock(&tpool->mutexBusy);
        task.entry((void *)task.arg);
        free(task.arg);
        task.arg = NULL;

        printf("tid %ld end working ..\n", pthread_self());
        pthread_mutex_lock(&tpool->mutexBusy);
        tpool->busyNum -= 1;
        pthread_mutex_unlock(&tpool->mutexBusy);
    }
    return NULL;
}

void* manager(void* arg) {
    ThreadPool* tpool = (ThreadPool *)arg;

    while(!tpool->shutdown) {
        // detect 
        sleep(5);

        // task numbers and thread numbers
        pthread_mutex_lock(&tpool->mutexPool);
        int qSize = tpool->qSize;
        int liveNum = tpool->liveNum;
        int busyNum = tpool->busyNum;
        pthread_mutex_unlock(&tpool->mutexPool);

        // add new threads
        if (qSize > liveNum && liveNum < tpool->maxNum) {
            pthread_mutex_lock(&tpool->mutexPool);
            for(int i = 0, counter = 0; i < tpool->maxNum && 
                                        counter < INC_NUM && 
                                        tpool->liveNum < tpool->maxNum; ++i) {
                if(tpool->threadIDs[i] == 0) {
                    pthread_create(&tpool->threadIDs[i], NULL, worker, tpool);
                    counter += 1;
                    tpool->liveNum += 1;
                }
            }
            pthread_mutex_unlock(&tpool->mutexPool);
        }

        // delete threads
        int realExit = 0;
        if (busyNum * 2 < liveNum && liveNum > tpool->minNum) {
            pthread_mutex_lock(&tpool->mutexPool);
            tpool->exitNum = (tpool->liveNum - tpool->minNum) < INC_NUM ? \
                             (tpool->liveNum - tpool->minNum) : INC_NUM;
            realExit = tpool->exitNum;
            pthread_mutex_unlock(&tpool->mutexPool); 

            for (int i = 0; i < realExit; ++i) {
                pthread_cond_signal(&tpool->notEmpty);
            }
        }
    }
    return NULL;
}

void thread_exit(ThreadPool* tpool) {
    pthread_t tid = pthread_self();
    for(int i = 0; i < tpool->maxNum; ++i) {
        if(tpool->threadIDs[i] == tid) {
            tpool->threadIDs[i] = 0;
            printf("thread_exit() called, %ld exiting ..\n", tid);
            break;
        }
    }
    pthread_exit(NULL);
}