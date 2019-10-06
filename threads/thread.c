#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <malloc.h>
#include "thread.h"
#include "interrupt.h"

// global variables
struct thread* 	thread0; // initial thread 
struct thread* 	threads[THREAD_MAX_THREADS] = { NULL }; // all threads 
struct queue* 	ready_queue;
struct queue* 	exit_queue;
Tid to_exit = 0;
int should_free = 0;

// structures
enum Status{
    RUNNING,
    READY,
    BLOCKED,
    EXITED
};

// thread control block
struct thread {
    Tid id;
    enum Status status;
    ucontext_t context;
    void * stack;
    volatile int setcontext_called;
};

struct queue {
    struct thread* thread;
    struct queue* next;
};

// function parameters
Tid get_id ();
void add_to_queue(struct queue* queue, struct thread* thread);
void delete_from_queue(struct queue* queue, Tid id);
void print_queue(struct queue* queue);
void free_thread(Tid id);

// return id of the running thread
Tid thread_id(){	
    for(int i = 0; i < THREAD_MAX_THREADS; i++) 
            if(threads[i] != NULL && threads[i]->status == RUNNING)
            return i;

    return THREAD_INVALID; 
}

// get an available id for a new thread being created
Tid get_id () {
    for(Tid i = 0; i < THREAD_MAX_THREADS; i++) 
    if(threads[i] == NULL || (threads[i]->status != RUNNING && threads[i]->status != READY)) 
        return i;
        
    return THREAD_NOMORE;
}

// get ID of first thread in list
Tid get_ready(struct queue* queue) {     
    if(queue == NULL || queue->next == NULL) 	return THREAD_NONE; 			
    else					return queue->next->thread->id; 
}

void add_to_queue(struct queue* queue, struct thread* thread) {
    struct queue* head;
    struct queue* temp;
    struct queue* new_thread;
	
    if(queue == NULL) 		return; 
    
    head = queue;
    new_thread  = (struct queue*)malloc(sizeof(struct queue));
    
    // initialize new node in queue
    new_thread->thread = thread;
    new_thread->next = NULL;
    
    if(queue->next == NULL) 
        queue->next = new_thread;
    else { 
		temp = head;
		while(temp->next != NULL) 
		    temp = temp->next;
		temp->next = new_thread;
		return;
    }
}

void delete_from_queue(struct queue* queue, Tid id) {
    struct queue* curr;
    struct queue* prev; // 

    if(queue == NULL || queue->next == NULL) 	return; 			// no thread in ready queue

    prev = queue;
    curr = queue->next; // first element of the list

    while(curr->thread->id != id && curr->next != NULL) {
            prev = curr;
            curr = curr->next;
    }

    if(curr->thread->id == id) {
            prev->next = curr->next;
            free(curr);
    }
}

void print_queue(struct queue* queue) {
    struct queue* curr = queue;

    if(curr == NULL || curr->next == NULL)  return; // no queue / no times in queue  						
    
    curr = curr->next;
    while(curr!= NULL && curr->next != NULL) {
       printf("%d -> ", curr->thread->id);
       curr = curr->next;
    }
    
    if(curr->thread != NULL)
        printf("%d\n", curr->thread->id);
}

void free_thread(Tid id) {
    free(threads[id]->stack);					
    free(threads[id]);
    threads[id] = NULL;
}

/*-----------------------------------------START THREAD LIBRARY FUNCTIONS----------------------------------------------------------*/

void thread_stub(void (*thread_main)(void *), void *arg) {
    thread_main(arg); 
    //printf("i am %d in stub\n", thread_id());
    
    thread_exit();
}

/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
};

void thread_init(void) {
    thread0 = (struct thread *)malloc(sizeof(struct thread));
    thread0->id = 0;
    thread0->status = RUNNING;
    thread0->setcontext_called = 0;
	assert(!getcontext(&(thread0->context)));
    threads[0] = thread0; 

	// create the ready queue
    ready_queue = (struct queue*)malloc(sizeof(struct queue));
    ready_queue->next = NULL; 

    exit_queue = (struct queue*)malloc(sizeof(struct queue));
    exit_queue->next = NULL; 
}

Tid thread_create(void (*fn) (void *), void *parg){	
    void *	thread_stack;
    struct  thread* thread;

    if(get_id() == THREAD_NOMORE)	return THREAD_NOMORE;
    
	thread_stack = (void*)malloc(THREAD_MIN_STACK);
    if(thread_stack == NULL) 		return THREAD_NOMEMORY;
    
    thread = (struct thread*)malloc(sizeof(struct thread)); 			// allocate mem for thread control block
    if(thread == NULL) {
    	free(thread_stack);
    	return THREAD_NOMEMORY;
    }
    
    // initialize control block    
    thread->id = get_id();
    thread->status = READY;
    thread->stack = thread_stack;
    thread->setcontext_called = 0;
				
    assert( !getcontext(&(thread->context)) );	
    	
    // manually edit general registers
    thread_stack += (THREAD_MIN_STACK - 8);	
    thread->context.uc_mcontext.gregs[REG_RIP] = (unsigned long)thread_stub;
    thread->context.uc_mcontext.gregs[REG_RDI] = (unsigned long)fn;    
    thread->context.uc_mcontext.gregs[REG_RSI] = (unsigned long)parg; 
    thread->context.uc_mcontext.gregs[REG_RSP] = (unsigned long)thread_stack;

    threads[thread->id] = thread;
    add_to_queue(ready_queue, thread);

    return thread->id;
}

Tid thread_yield(Tid want_tid){  
    //free_exited();
    
    // printf("i am %d in yield\n", thread_id());
    Tid ready_id;
    Tid running_id = thread_id();
    
	// conditions for yielding
    int valid_id 		= (want_tid >= 0 && want_tid < THREAD_MAX_THREADS); // is the want_tid within the valid range (for indexing a specific thread)
    int YIELD_SELF 		= (want_tid == THREAD_SELF) || (valid_id && thread_id() == want_tid);
    int YIELD_ANY 		= (want_tid == THREAD_ANY);
    int YIELD_SPECIFIC 	= (valid_id) && (threads[want_tid] != NULL) && (threads[want_tid]->status == READY); 

    // figure out which ready thread to yield to 
    if (YIELD_SELF) 				return running_id;
    else if (YIELD_ANY) 			ready_id = get_ready(ready_queue); 												  
    else if(YIELD_SPECIFIC) 		ready_id = want_tid;
    else							return THREAD_INVALID;

    if(ready_id == THREAD_NONE)		return THREAD_NONE; // return from get_ready(ready_queue)					 

    struct thread* ready_thread = threads[ready_id];
    delete_from_queue(ready_queue, ready_id);			// delete thread from ready queue

    assert( !getcontext(&(threads[running_id]->context)) );

    if(should_free) {
        free_thread(to_exit);
        should_free = 0;
    }

    if(ready_thread->setcontext_called == 1) { 			// if setcontext has been called, this is the second return of getcontext so exit function
            ready_thread->setcontext_called = 0;
            return ready_thread->id;
    }
    else {												// else, set context
            ready_thread->setcontext_called = 1;
            ready_thread->status = RUNNING;
            threads[running_id]->status = READY;
            add_to_queue(ready_queue, threads[running_id]);
            assert( !setcontext(&(ready_thread->context)) );	
    }
       
    return THREAD_NONE;
}
/*----------------------------------------------------------------------------------------------*/
Tid free_exited() {
    for(Tid i = 0; i < THREAD_MAX_THREADS; i++) 
        if(threads[i] != NULL && threads[i]->status == EXITED) {
            printf("free me: %d\n", i);
            free_thread(i);
            return i;
        }
    return THREAD_NONE;
}

Tid free_all() {
    for(Tid i = 0; i < THREAD_MAX_THREADS; i++) 
    if(threads[i] != NULL) {
            printf("free all: %d\n", i);
            delete_from_queue(ready_queue, i);
            free_thread(i);
            return i;
    }
    return THREAD_NONE;
}

Tid whats_left() {
    printf("what's left: ");
    for(Tid i = 0; i < THREAD_MAX_THREADS; i++) {
    if(threads[i] != NULL) {
            printf("%d -> ", i);
            return i;
    }
    }
    return THREAD_NONE;
}
/*----------------------------------------------------------------------------------------------*/

void thread_exit() {
    if(should_free) {
        free_thread(to_exit);
        should_free = 0;
    }
    
    //printf("i am %d and i am exiting\n", thread_id());
    Tid ready_id = get_ready(ready_queue); 					// get the first thread from ready queue to yield to	
    if(ready_id == THREAD_NONE)	return exit(0); 			// if the only thread left is the one running, exit the program

    to_exit = thread_id();
    should_free = 1;
    threads[to_exit]->status = EXITED;

    threads[ready_id]->status = RUNNING;					// update status of the ready thread to running			
    delete_from_queue(ready_queue, threads[ready_id]->id);	// delete ready thread form ready queue				
    assert( !setcontext(&(threads[ready_id]->context)) );	// set context to the ready thread
}

Tid thread_kill(Tid tid) {	
    // check that tid is 1) within bounds, 2) matches an existing thread, and 3) is not the running thread
    int valid_id = (tid >= 0 && tid < THREAD_MAX_THREADS) && (threads[tid] != NULL) && (tid != thread_id());
    if(!valid_id)	return THREAD_INVALID;

    delete_from_queue(ready_queue, tid);		// delete killed thread from ready queue
    free_thread(tid);

    return tid;
}

// Nope
/*
void thread_exit() {
	//printf("i am %d exiting\n", thread_id());
	//whats_left();
	
	Tid ready_id = get_ready(ready_queue); 					// get the first thread from ready queue to yield to	
	if(ready_id == THREAD_NONE)	{
		//free_all();
		exit(0);
	}

	// mark the current thread as exited and remove it from the ready queue (it actually wouldnt be in the ready queue)
	//threads[thread_id()]->status = EXITED;
	to_exit = thread_id();
	
	threads[ready_id]->status = RUNNING;					// update status of the ready thread to running			
	delete_from_queue(ready_queue, threads[ready_id]->id);	// delete ready thread form ready queue				
	assert( !setcontext(&(threads[ready_id]->context)) );	// set context to the ready thread
}
*/

// Working but wrong implementation
/*
void thread_exit() {
	Tid ready_id = get_ready(ready_queue); 					// get the first thread from ready queue to yield to	
	if(ready_id == THREAD_NONE)	return exit(0); 			// if the only thread left is the one running, exit the program

	//add_to_queue(exit_queue, threads[thread_id()]);			// add the running thread to the exit queue;
	//print_queue(exit_queue);
	
	free_thread(thread_id());								// free currently running thread thread from memory and threads[] array
	
	threads[ready_id]->status = RUNNING;					// update status of the ready thread to running			
	delete_from_queue(ready_queue, threads[ready_id]->id);	// delete ready thread form ready queue				
	assert( !setcontext(&(threads[ready_id]->context)) );	// set context to the ready thread
}
*/



// Working but wrong implementation
/*
Tid thread_kill(Tid tid) {	
	// check that tid is 1) within bounds, 2) matches an existing thread, and 3) is not the running thread
	int valid_id = (tid >= 0 && tid < THREAD_MAX_THREADS) && (threads[tid] != NULL) && (tid != thread_id());
	if(!valid_id)	return THREAD_INVALID;

	delete_from_queue(ready_queue, threads[tid]->id);		// delete killed thread from ready queue
	free_thread(tid);										// free killed thread from memory and threads[] array
	
	return tid;
}
*/

/*******************************************************************************************************************************************************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 ***************************************************************************************************************************************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
