#include "co.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define STACK_SIZE ((1 << 20) + 1)
#define CO_MAX 128 

#define EXIT_YEILD return 
#define panic(...) { printf(__VA_ARGS__); assert(0); }
#define noop {}
#define check_co(next) { \
  ((next)->precond) == true ? (goto reselect) : ((next)->status) = CO_RUNNING; \
}

#ifdef LOCAL_MACHINE
  #define debug(...) printf(__VA_ARGS__)
#else 
  #define debug(...)
#endif 

/************************* API *************************/
static void init_switch(struct co *next);
/*******************************************************/

enum co_status {
  CO_NEW     = 1,                 /*   new   */
  CO_RUNNING = 2,                 /* running */
  CO_WAITING = 3,                 /* waiting */
  CO_DEAD    = 4,                 /*  dead   */
};

struct co {
  char cname[32];                 /* coroutine name      */
  uint32_t cid;                   /* coroutine id        */
  void (*entry)(void *);          /* coroutine entry     */
  void *args;                     /* entry function args */

  enum co_status status;
  struct co *    waiter;
  jmp_buf        context;
  int32_t        precond;         /* num waiting for complete */
  uint8_t        stack[STACK_SIZE] __attribute__((aligned(16)));
};

struct wait_co_node {
  struct co * this;
  struct wait_co_node * next;
};

/****************** GLOBAL VARIABLES *******************/
static struct co *current = NULL;      // execute co in cpu
static struct wait_co_node wait_head = { \
                .this = NULL, .next = NULL \
              };                       // waiting list 
static uint32_t wait_nco = 0;          // wait number
static uint8_t assign_cid[CO_MAX];     // cid assign flag

/*******************************************************/
static void show_waiting_list() {
  struct wait_co_node *first = wait_head.next;
  debug("\twaiting list [%u]: ", wait_nco);
  while(first != NULL && first->this != NULL) {
    debug("(%s %u)->", first->this->cname, first->this->cid);
    first = first->next;
  }
  debug("None\n\n");
}

static void insert_wait_list(struct co *cot) {
  struct wait_co_node *new_node = \
      (struct wait_co_node *)malloc(sizeof(struct wait_co_node));
  new_node->this = cot; 
  new_node->next = wait_head.next;
  wait_head.next = new_node;
  wait_nco += 1;

  debug("\tinsert (%s, %u)\n", cot->cname, cot->cid);
}

static void pop_wait_list(struct co *cot) {
  uintptr_t dels = (uintptr_t)cot;
  struct wait_co_node *prev = &wait_head;
  struct wait_co_node *curr = wait_head.next;
  while (curr != NULL && (intptr_t)curr->this != dels) {
    prev = curr;
    curr = curr->next;
  }
  if (curr == NULL) 
    return;
  if((intptr_t)curr->this == dels) {
    prev->next = curr->next;
    free(curr);
    wait_nco -= 1;
  } else {
    panic("delete error.\n");
  }
}

static struct co *choose_co() {
  int rnum;
  struct wait_co_node *cptr, *pptr;
  struct co *rptr = NULL;

reselect:
  rnum = rand() % wait_nco + 1;             // range from 1 to wait_nco    
  cptr = wait_head.next;
  pptr = &wait_head;

  while(--rnum) {
    pptr = cptr;
    cptr = cptr->next;
  }

  assert(cptr->this != NULL);

  if (cptr->this->status == CO_DEAD || cptr->this->precond > 0) {
    goto reselect;
  } else {
    pptr->next = cptr->next;
    rptr = cptr->this;
    free(cptr);
    wait_nco -= 1;
  }

  return rptr;
}

struct co *co_start(const char *name, void (*func)(void *), void *args) {
  // alloc memory for 'strcut co'
  struct co *newco = (struct co *)malloc(sizeof(struct co));
  
  // initlize
  strncpy(newco->cname, name, strlen(name));
  newco->entry    = func;
  newco->args     = args;
  newco->status   = CO_NEW;
  newco->waiter   = NULL;
  newco->precond  = 0;                  // without waiting coroutine

  int idx = 0;
  while(assign_cid[++idx] == 1) ;       // search empty id (0 is main routine)
  
  if (idx == CO_MAX) {
    panic("The created concurrent process reaches maximum.\n");
  }
  newco->cid = idx; 
  assign_cid[idx] = 1;

  debug("create (%s, %u)\n", newco->cname, newco->cid);

  if (!current) {                       // cpu is leisure and hold cpu
    current = newco; 
    current->status = CO_RUNNING;
  } else {                              // insert waiting list
    insert_wait_list(newco);  
  }
  
  return newco;
}

static void free_co(struct co *this) {
  this->waiter = NULL;
  assign_cid[this->cid] = 0;
  pop_wait_list(this);
  debug("-->free (%s, %u)\n", this->cname, this->cid);
#ifdef LOCAL_MACHINE
  show_waiting_list();
#endif
  free(this);
}

/********************* init and fin ********************/
__attribute__((constructor)) void init_func() {
  extern int main();
  void (*main_ptr)(void *) = (void (*)(void *))main;
  void *args = NULL;
  struct co *main_co = co_start("main", main_ptr, args);
}

__attribute__((destructor)) void fin_func() {
  free_co(current);
}
/*******************************************************/

void co_wait(struct co *co) {
  assert(current != NULL);
  debug("(%s, %u) is waiting (%s, %u)\n", current->cname, current->cid, \
                                          co->cname, co->cid);
  if (current == co) {
    panic("Coroutine can't wait for itself.\n");
  } else {
    switch (co->status) {
      // if precond is true, coroutine engine not yeild here.
      case CO_NEW: case CO_WAITING: 
                    { co->waiter = current; current->precond += 1; co_yield(); }; break;
      case CO_DEAD: { noop };                                                     break;
      default:      { panic("Coroutine status must in [New, Waiting, Dead].\n")};
    }
    // here co is finish -> yeild
    free_co(co);
  }
}

static void init_switch(struct co *next) {
  asm volatile (
#if __x86_64__
    "movq %0, %%rsp; movq %2, %%rdi; call *%1"
      : : "b"((uintptr_t)(next->stack) + STACK_SIZE - 1), "d"(next->entry), "a"(next->args) : "memory"
#else 
    "movl %0, %%esp; movl %2, 0(%0); call *%1"
      : : "b"((uintptr_t)(next->stack) - 8 + STACK_SIZE - 1), "d"(next->entry), "a"(next->args) : "memory"
#endif 
  );
}

void co_yield() {  
  /* step 1. save context environment */
  int status;
  if ((status = setjmp(current->context)) != 0) {
    // control is given to switched coroutine
    return ;
  }

  if (current->status != CO_DEAD) {
    current->status = CO_WAITING;
  }
  insert_wait_list(current);
  current = NULL;                // the cpu is leisure

  /* step 2. coroutine manage engine. */
  struct co *next = choose_co();
  assert(next->status == CO_NEW || next->status == CO_WAITING);
  debug("\tyield to (%s, %u)\n", next->cname, next->cid);
  show_waiting_list();
  
  switch (next->status) {
    case CO_NEW:     { 
                        current = next;
                        current->status = CO_RUNNING;
                        init_switch(next); 
                        // when init_switch return, then next is finish
                        current->status = CO_DEAD;
                        if (current->waiter != NULL) {
                          current->waiter->precond -= 1;
                        };
                        co_yield();
                     }; break; 
    case CO_WAITING: { 
                        current = next;
                        current->status = CO_RUNNING;
                        longjmp(current->context, current->cid);
                     }; break; 
    default:         {  panic("Wrong concurrent status.\n"); }
  }
}
