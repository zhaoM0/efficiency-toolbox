#ifndef _CO_H_
#define _CO_H_

struct co* co_start(const char *name, void (*func)(void *), void *arg);
void       co_yield();
void       co_wait(struct co *co);

#endif /* end of file. */