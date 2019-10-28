// git reset --hard cedc856
// git push --force origin master

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

int count_stack = 0;
int count_thread = 0;
int count_queue = 0;
struct queue* 	ready_queue;
Tid to_exit = 0;
int should_free = 0;

// structures
enum Status{
    RUNNING = 1,
    READY = 2,
    BLOCKED = 3,
    EXITED = 4
};

struct queue {
    struct thread* thread;
    struct queue* next;
};

// thread control block
struct thread {
    Tid id;
    enum Status status;
    ucontext_t context;
    void * stack;
    volatile int setcontext_called;
    int killmepls;
    struct wait_queue* queue;
};

/* This is the wait queue structure */
struct wait_queue {
	struct queue* 	wait; // using the struct i already created for queues
};

// function parameters
Tid get_id ();
void add_to_queue(struct queue* queue, struct thread* thread);
void delete_from_queue(struct queue* queue, Tid id);
void print_queue(struct queue* queue);
void free_thread(Tid id);
Tid free_exited();
Tid yeet();
int queue_count(struct queue* queue);
void delete_from_all_queues(Tid id);

// return id of the running thread
Tid thread_id(){	
    for(int i = 0; i < THREAD_MAX_THREADS; i++) 
        if(threads[i] != NULL && threads[i]->status == RUNNING)
            return i;

    return THREAD_INVALID; 
}

int queue_count(struct queue* queue) {
    int count = 0;
    struct queue* curr;
    curr = queue; 

    if(queue == NULL || queue->next == NULL) {
        return 0;
    }

    while(curr->next != NULL) {
        count++;
        curr = curr->next;
    }
    return count;
}

// get an available id for a new thread being created
Tid get_id () {
    for(Tid i = 0; i < THREAD_MAX_THREADS; i++) 
    if(threads[i] == NULL || (threads[i]->status != RUNNING && threads[i]->status != READY && threads[i]->status != BLOCKED)) 
        return i;
        
    return THREAD_NOMORE;
}

// get ID of first thread in list
Tid get_first(struct queue* queue) {     
    if(queue == NULL || queue->next == NULL) 	return THREAD_NONE; 			
    else					                    return queue->next->thread->id; 
}

void add_to_queue(struct queue* queue, struct thread* thread) {
    struct queue* head;
    struct queue* temp;
    struct queue* new_thread;
	
    if(queue == NULL) 		return; 
    
    head = queue;
    new_thread  = (struct queue*)malloc(sizeof(struct queue));
    count_queue++;

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
        curr = NULL;
        count_queue--;
    }
}

void delete_from_all_queues(Tid id) {
    for(int i = 0; i < THREAD_MAX_THREADS; i++) {
        if(threads[i] != NULL) {
            struct queue* curr = threads[i]->queue->wait;

            if(curr->next != NULL) {
                curr = curr->next;
            }
            
            while(curr != NULL) {
                if(curr->thread != NULL && curr->thread->id == id) {      
                    delete_from_queue(threads[i]->queue->wait, id);
                }
                
                curr = curr->next;
            }
        }
    }
}

void print_queue(struct queue* queue) {
    struct queue* curr = queue;

    if(curr == NULL || curr->next == NULL) {
        printf("EMPTY\n");
        return; // no queue / no times in queue  
    }
    
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
    threads[id]->stack = NULL;
    free(threads[id]->queue->wait);	
    free(threads[id]->queue);			
    free(threads[id]);
    threads[id] = NULL;
}

/*-----------------------------------------START THREAD LIBRARY FUNCTIONS----------------------------------------------------------*/
void thread_stub(void (*thread_main)(void *), void *arg) {
    interrupts_on();
    thread_main(arg);     
    thread_exit();
}

void thread_init(void) {
    thread0 = (struct thread *)malloc(sizeof(struct thread));
    thread0->queue = malloc(sizeof(struct wait_queue));
    thread0->queue->wait = (struct queue*)malloc(sizeof(struct queue));
    thread0->id = 0;
    thread0->status = RUNNING;
    thread0->setcontext_called = 0;
    assert(!getcontext(&(thread0->context)));
    threads[0] = thread0; 

    // create the ready queue
    ready_queue = (struct queue*)malloc(sizeof(struct queue));
    ready_queue->next = NULL; 
}

Tid thread_create(void (*fn) (void *), void *parg){	
    int enabled = interrupts_off();
    void *	thread_stack;
    struct  thread* thread;

    if(get_id() == THREAD_NOMORE) {
        interrupts_set(enabled);
        return THREAD_NOMORE;
    }
    
	thread_stack = (void*)malloc(THREAD_MIN_STACK);
    count_stack++;
    if(thread_stack == NULL) {
        interrupts_set(enabled);
        free(thread_stack);
        thread_stack = NULL;
        count_stack--;
        return THREAD_NOMEMORY;
    }
    
    thread = (struct thread*)malloc(sizeof(struct thread)); 			// allocate mem for thread control block
    count_thread++;
    if(thread == NULL) {
    	free(thread_stack);
        count_stack--;
        thread_stack = NULL;

        free(thread);
        count_thread--;
        thread = NULL;
        interrupts_set(enabled);
    	return THREAD_NOMEMORY;
    }
    
    // initialize control block    
    thread->id = get_id();
    thread->queue = malloc(sizeof(struct wait_queue));
    thread->queue->wait = (struct queue*)malloc(sizeof(struct queue));
    thread->status = READY;
    thread->stack = thread_stack;
    thread->setcontext_called = 0;
    thread->killmepls = 0;
				
    assert( !getcontext(&(thread->context)) );	
    	
    // manually edit general registers
    thread_stack += (THREAD_MIN_STACK - 8);	
    thread->context.uc_mcontext.gregs[REG_RIP] = (unsigned long)thread_stub;
    thread->context.uc_mcontext.gregs[REG_RDI] = (unsigned long)fn;    
    thread->context.uc_mcontext.gregs[REG_RSI] = (unsigned long)parg; 
    thread->context.uc_mcontext.gregs[REG_RSP] = (unsigned long)thread_stack;

    threads[thread->id] = thread;
    add_to_queue(ready_queue, thread);

    interrupts_set(enabled);
    return thread->id;
}

Tid thread_yield(Tid want_tid){     
    int enabled = interrupts_off();
    // unintr_printf("%d in yield\n", thread_id());
    Tid ready_id;
    Tid running_id = thread_id();
    
    free_exited();
    // conditions for yielding
    int valid_id 		= (want_tid >= 0 && want_tid < THREAD_MAX_THREADS); // is the want_tid within the valid range (for indexing a specific thread)
    int YIELD_SELF 		= (want_tid == THREAD_SELF) || (valid_id && thread_id() == want_tid);
    int YIELD_ANY 		= (want_tid == THREAD_ANY);
    int YIELD_SPECIFIC 	= (valid_id) && (threads[want_tid] != NULL) && (threads[want_tid]->status == READY); 

    // figure out which ready thread to yield to 
    if (YIELD_SELF) {
        interrupts_set(enabled);
        return running_id;
    }
    else if (YIELD_ANY) 			ready_id = get_first(ready_queue); 												  
    else if(YIELD_SPECIFIC) 		ready_id = want_tid;
    else {
        interrupts_set(enabled);
        return THREAD_INVALID;
    }

    if(ready_id == THREAD_NONE) {
        interrupts_set(enabled);
        return THREAD_NONE; // return from get_first(ready_queue)		
    }			 

    struct thread* ready_thread = threads[ready_id];
    delete_from_queue(ready_queue, ready_id);			// delete thread from ready queue

    free_exited(); // cause get context returns here

    volatile int set = 0;

    assert( !getcontext(&(threads[running_id]->context)) );
    
    if(set) { 			// for setcontexts from other funcs like exit
        // ready_thread->setcontext_called = 0;
        set = 0;
        interrupts_set(enabled);
        return ready_thread->id;
    }
    else {												// else, set context
        set = 1;
        ready_thread->status = RUNNING;
        threads[running_id]->status = READY;
        add_to_queue(ready_queue, threads[running_id]);
        assert( !setcontext(&(ready_thread->context)) );	
    }
    
    interrupts_set(enabled);
    return THREAD_NONE;
}
/*----------------------------------------------------------------------------------------------*/
Tid free_exited() {
    for(Tid i = 0; i < THREAD_MAX_THREADS; i++) 
        if(threads[i] != NULL && threads[i]->status == EXITED) {
            delete_from_queue(ready_queue, i);

            // just for safe measure
            delete_from_all_queues(i);
            thread_wakeup(threads[i]->queue, 1);

            free_thread(i);
            return i;
        }
    return THREAD_NONE;
}
/*----------------------------------------------------------------------------------------------*/

void thread_exit() {
    int enabled = interrupts_off();
    free_exited();                                  // delete any threads waiting to be killed
    threads[thread_id()]->killmepls = 1;            // mark curr thread as to be killed
    delete_from_all_queues(thread_id());
    // printf("** exit %d. %d's wait queue\n", thread_id(), thread_id());
    // print_queue(threads[thread_id()]->queue->wait);

    delete_from_all_queues(thread_id());
    thread_wakeup(threads[thread_id()]->queue, 1);

    threads[thread_id()]->status = EXITED;

    // yield to a ready thread 
    if(yeet() == THREAD_NONE) {
        // printf("oh no, no threads to yield to\n");
        exit(0);         
    }                     
    interrupts_set(enabled);
}

// yield to another thread from thread_exit
Tid yeet() {   
    Tid ready_id = get_first(ready_queue); 					// get the first thread from ready queue to yield to
    if(ready_id == THREAD_NONE)	{
        return THREAD_NONE; 			// if the only thread left is the one running, exit the program
    }

    threads[ready_id]->status = RUNNING;
    delete_from_queue(ready_queue, threads[ready_id]->id);	// delete ready thread form ready queue	
    assert( !setcontext(&(threads[ready_id]->context)) );	

    return ready_id;
}

Tid thread_kill(Tid tid) {	
    int enabled = interrupts_off();
    // check that tid is 1) within bounds, 2) matches an existing thread, and 3) is not the running thread
    int valid_id = (tid >= 0 && tid < THREAD_MAX_THREADS) && (threads[tid] != NULL) && (tid != thread_id());
    if(!valid_id) {
        interrupts_set(enabled);
        return THREAD_INVALID;
    }

    threads[tid]->status = EXITED;
    delete_from_queue(ready_queue, tid);

    thread_wakeup(threads[tid]->queue, 1); 
    delete_from_all_queues(tid); 

    free_exited();
    interrupts_set(enabled);
    return tid;
}

/*******************************************************************************************************************************************************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 ***************************************************************************************************************************************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue * wait_queue_create() {
	int enabled = interrupts_off();
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	//TBD();
    wq->wait = (struct queue*)malloc(sizeof(struct queue));
    wq->wait->next = NULL; 
    
    interrupts_set(enabled);
	return wq;
}

void wait_queue_destroy(struct wait_queue *wq) {
    free_exited();
    while(get_first(wq->wait) != THREAD_NONE) {
        Tid killme = get_first(wq->wait);
        delete_from_queue(wq->wait, killme);
        free_thread(killme);
    }

    free(ready_queue);
    ready_queue = NULL;
    free(wq->wait);
    wq->wait = NULL;
	free(wq);
    free(ready_queue);
    wq = NULL;
}

Tid thread_sleep(struct wait_queue *queue) {
    int enabled = interrupts_off();

    // queue invalid
    if(queue == NULL) {
        interrupts_set(enabled);
        return THREAD_INVALID;
    }

    // get first thread in ready queue
    Tid ready_id = get_first(ready_queue); 					

    // no thread in ready queue
    if(ready_id == THREAD_NONE) {
        interrupts_set(enabled);
        return THREAD_NONE;
    }

    struct thread* ready_thread = threads[ready_id];
    struct thread* wait_thread = threads[thread_id()];

    volatile int set = 0;
    assert( !getcontext(&(wait_thread->context)) );
    
    if(set) { 		
        set = 0;	
        interrupts_set(enabled);
        return ready_id;
    }
    else {												
        set = 1;
        add_to_queue(queue->wait, wait_thread); // running thread to wait queue
        delete_from_queue(ready_queue, ready_thread->id);	// delete ready thread form ready queue
        ready_thread->status = RUNNING;
        wait_thread->status = BLOCKED;
        assert( !setcontext(&(ready_thread->context)) );	
    }
    
    interrupts_set(enabled);
    return ready_id;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int thread_wakeup(struct wait_queue *queue, int all) {
	int enabled = interrupts_off();
    int count = 0;

    // queue is null or theres nothing in the wait queue
    if(queue == NULL || queue->wait == NULL || queue->wait->next == NULL) {
        interrupts_set(enabled);
        return 0;
    }

    if(all) {
        Tid wake_id = get_first(queue->wait);
        while(wake_id != THREAD_NONE) {
            count++;
            // printf("WAKE %d\n", wake_id);
            delete_from_queue(queue->wait, wake_id); // take it out of the wait queue
            add_to_queue(ready_queue, threads[wake_id]);
            threads[wake_id]->status = READY;
            wake_id = get_first(queue->wait);
        }
        interrupts_set(enabled);
        return count;
    }
    else {
        Tid wake_id = get_first(queue->wait);
        if(wake_id != THREAD_NONE) {
            // printf("WAKE %d\n", wake_id);
            delete_from_queue(queue->wait, wake_id); // take it out of the wait queue
            add_to_queue(ready_queue, threads[wake_id]);
            threads[wake_id]->status = READY;
        }
        interrupts_set(enabled);
        return 1;
    }
    interrupts_set(enabled);
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid thread_wait(Tid tid) {
	int enabled = interrupts_off();
    int valid_id = (tid >= 0 && tid < THREAD_MAX_THREADS) && (threads[tid] != NULL) && (tid != thread_id());
    if(!valid_id) {
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    thread_sleep(threads[tid]->queue);    
    interrupts_set(enabled);
	return tid;
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
