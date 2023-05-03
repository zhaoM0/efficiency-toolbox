#include "tpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void entry(void* arg) {
    int num = *((int *)arg);
    int sum = 0;
    for (int i = 0; i < num; i++)
        sum += i;
    printf("\ttid %ld is working, sum = %d\n", pthread_self(), sum);
    sleep(1);
}

int main() {
    ThreadPool* pool = tp_create(3, 6, 20);
    for(int i = 0; i < 50; i++) {
        int *num = (int *)malloc(sizeof(int));
        *num = i + 10;
        tp_add(pool, entry, num);
    }

    sleep(30);

    tp_destroy(pool);
    return 0;
}