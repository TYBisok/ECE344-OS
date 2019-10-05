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
struct thread* 	all_threads[THREAD_MAX_THREADS] = { NULL }; // all threads 
struct queue* 	ready_queue;
struct queue* 	exited_queue;

enum Status{
    RUNNING,
    READY,
    WATING,
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

// done and makes sense
Tid get_id () {
	for(Tid i = 0; i < THREAD_MAX_THREADS; i++) {
        if(all_threads[i] == NULL || (all_threads[i]->status != RUNNING && all_threads[i]->status != READY)) {
            return i;
        }
    }
        
    return THREAD_NOMORE;
}

struct queue {
    struct thread* thread;
    struct queue* next;
};

Tid delete_thread(struct queue* queue, Tid id) {
	struct queue* curr;
	struct queue* prev;
	
   if(queue == NULL || queue->next == NULL) 	return THREAD_NONE; 			// no thread in ready queue
   else {
   		prev = queue;
		curr=queue->next; // first element of the list

		while(curr->thread->id != id && curr->next != NULL) {
			prev = curr;
			curr = curr->next;
		}

		if(curr->thread->id == id) {
			prev->next = curr->next;
			// dont wanna free the thread ptr and the next ptr cause they're still in us
			free(curr);
			return id;
		}

		return THREAD_NONE;
   }
}

Tid get_ready(struct queue* queue) {
    struct queue* to_delete;
    //printf("wtf\n");
    
    if(queue == NULL || queue->next == NULL) 	return THREAD_NONE; 			// no thread in ready queue
    else {
        to_delete = queue->next; // this is the thread to delete (and also return the id of)
        Tid ready_id = to_delete->thread->id; // id to return
        
        if(to_delete->next != NULL)
        	queue->next = to_delete->next;	// head now points to the second thread in the list 
        else 
        	queue->next = NULL;
        
            
        //printf("******deleting thread %d from ready queue\n", ready_id);
        // do i need to free the thread too? no right cause its in my array as well?
        free(to_delete);
        return ready_id;
    }
}

void add_queue(struct queue* queue, struct thread* thread) {
	//printf("1\n");
	//printf("trying to add %d\n", thread->id);
    if(queue == NULL) 		return; 

    //if (thread->id == in_queue(ready_queue, thread->id))	return; // if its already in the queue then don't add it (this shouldnt even be a condition, just find the bug)
    //printf("2\n");
    // allocate new node
    struct queue* head = queue;
    struct queue* new_thread = (struct queue*)malloc(sizeof(struct queue));
    
    // initialize new node
    new_thread->thread = thread;
    new_thread->next = NULL;

    //printf("--adding %d to queue\n", thread->id);
    
    if(queue->next == NULL) { 
        queue->next = new_thread;
        //printf("adding %d to start\n", thread->id);
        //free(new_thread);
        return;
    }
    else { // find the last node
		struct queue* temp = head;
		while(temp->next != NULL) {
		    temp = temp->next;
		}
		temp->next = new_thread;
		//printf("adding %d to end\n", thread->id);
		//free(new_thread);
		return;
    }
}

void print_queue(struct queue* queue) {
    struct queue* curr = queue;

    if(curr == NULL) {
    	printf("no list\n");
		return; // no list
    } 				
    	
    if(curr->next == NULL) {
     	printf("empty list\n");
 		return; // no list
     } 				
    
    curr = curr->next;
    printf("QUEUE: ");
    //printf("\n----------------READY QUEUE------------------\n");
    while(curr!= NULL && curr->next != NULL) {
       printf("%d -> ", curr->thread->id);
       curr = curr->next;
    }
    
    if(curr->thread != NULL)
        printf("%d\n", curr->thread->id);
        
    //printf("\n----------------END QUEUE------------------\n\n");
}

void
thread_stub(void (*thread_main)(void *), void *arg){
	//Tid ret;

	thread_main(arg); // call thread_main() function with arg
	thread_exit();
}

struct thread* ready_thread() {
	if(ready_queue->next == NULL)
	  return NULL;    
	else
	  return ready_queue->next->thread;
};

/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
};

void
thread_init(void) {
	// initialize thread0
	thread0 = (struct thread *)malloc(sizeof(struct thread));

    thread0->id = 0;
    thread0->status = RUNNING;
    thread0->setcontext_called = 0;
    int err = getcontext(&(thread0->context));
	assert(!err);
    all_threads[0] = thread0; 

    // initialize ready queue
    ready_queue = (struct queue*)malloc(sizeof(struct queue));
    ready_queue->next = NULL; // head

    exited_queue = (struct queue*)malloc(sizeof(struct queue));
    exited_queue->next = NULL; // head
}

// return id of the running thread
Tid
thread_id(){
	//return running_thread->id;
	
	for(int i = 0; i < THREAD_MAX_THREADS; i++) {
	  if(all_threads[i] != NULL && all_threads[i]->status == RUNNING)
	      return i;
	}
   
	return THREAD_INVALID; 
}

Tid
thread_create(void (*fn) (void *), void *parg){	
    void *	thread_stack;
    struct  thread* thread;
    Tid     thread_id;
    int     err;
    
	//thread_stack = aligned_alloc((size_t)16, (size_t)THREAD_MIN_STACK); // allocate 16 byte aligned mem for stack
	thread_stack = malloc(THREAD_MIN_STACK);
    if(thread_stack == NULL) return THREAD_NOMEMORY;
    
    thread = (struct thread*)malloc(sizeof(struct thread)); 			// allocate mem for thread control block
    if(thread == NULL) {
    	free(thread_stack);
    	return THREAD_NOMEMORY;
    }
        
    thread_id = get_id();												// get a Tid for the new thread	
    if(thread_id == THREAD_NOMORE) {
    	free(thread_stack);
    	free(thread);
    	return THREAD_NOMORE;
    }
    
    // initialize control block
    thread->id = thread_id;
    thread->status = READY;
    thread->stack = thread_stack;
    thread->setcontext_called = 0;
			
  	thread_stack += (THREAD_MIN_STACK - 8);					
    err = getcontext(&(thread->context));
    assert(!err);	
    	
    // manually edit general registers
    thread->context.uc_mcontext.gregs[REG_RIP] = (unsigned long)&thread_stub;
    thread->context.uc_mcontext.gregs[REG_RDI] = (unsigned long)fn;    
    thread->context.uc_mcontext.gregs[REG_RSI] = (unsigned long)parg; 
	thread->context.uc_mcontext.gregs[REG_RSP] = (unsigned long)thread_stack;
    
    // add created thread to ready queue
    add_queue(ready_queue, thread);

    //if(thread_id == 1023) {
    	//printf("curr running: %d\n", curr_running_id);
    	//print_queue(ready_queue);
    //}
    	
    //print_queue(ready_queue);
    all_threads[thread_id] = thread;

    //printf("created %d\n", thread_id);
    return thread_id;
}

Tid
thread_yield(Tid want_tid){    
    int            err, valid_id;
    struct thread* running_thread;
    struct thread* ready_thread;
    
    running_thread = all_threads[ thread_id() ];               // this is the current running thread
    valid_id = want_tid >= 0 && want_tid < THREAD_MAX_THREADS; // is the want_tid within the valid range (for indexing a specific thread)
    
    if (want_tid == THREAD_SELF || (valid_id && thread_id() == want_tid)){
		return running_thread->id;
   	}
    
    if (want_tid == THREAD_ANY) {
        Tid ready_id = get_ready(ready_queue); 												  // get first thread in the ready queue and delete it from the queue
        if(ready_id == THREAD_NONE) 					return THREAD_NONE;					  // no threads in the ready queue
        ready_thread = all_threads[ready_id]; 	// got em!!
    }
    else {
    	if (!valid_id) 											return THREAD_INVALID;				  // the id is not valid
    	if(all_threads[want_tid] == NULL)						return THREAD_INVALID;
        if(valid_id && all_threads[want_tid]->status != READY) 	return THREAD_INVALID;				  // the id is valid but the thread is not ready
        if(valid_id && all_threads[want_tid]->status == READY) 	ready_thread = all_threads[want_tid]; // yield to a specific ready thread

        //printf("\nyield SPECIFIC. running: %d, ready: %d\n", running_thread->id, ready_thread->id);
         
		delete_thread(ready_queue, want_tid); // to delete a specific thread from queue (usually get_ready would do that)
    }

    // now switch contexts - should only get here from THREAD_ANY or a specific thread
    //printf("******suspended id: %d\n", running_thread->id);
//    setcontext_called[running_thread->id] = 0; // get context has been called up there!!!!		;

	//printf("running is %d and the ready is %d\n", running_thread->id, ready_thread->id);

	//printf("start yield process from %d to %d - set called of %d - %d", running_thread->id, ready_thread->id, ready_thread->id, ready_thread->setcontext_called);
	err = getcontext(&(running_thread->context));
	assert(!err);

	if(ready_thread->setcontext_called == 1) { // then set context has already been called for this thread and it's currently running so set it to 0 now and move along
		ready_thread->setcontext_called = 0;
		return ready_thread->id;
	}
	else {
		ready_thread->setcontext_called = 1;
		ready_thread->status = RUNNING;
		running_thread->status = READY;
		add_queue(ready_queue, running_thread);

	    err = setcontext(&(ready_thread->context));
	   	assert(!err);	
	}
       
    return THREAD_NONE;
}

void
thread_exit() {
	Tid running_id = thread_id();
	struct thread* exited_thread = all_threads[running_id]; // get the currently running thread

	if(exited_thread->id == 1022) {
		//printf("exiting %d here's the ready queue\n", exited_thread->id);
		//print_queue(ready_queue); 
	}
		
	exited_thread->status = EXITED;
	
	// find a thread to run next 
	Tid ready_id = get_ready(ready_queue); 												  // get first thread in the ready queue and delete it from the queue
	if(ready_id == THREAD_NONE) {
		exit(0);
	}
	
	struct thread* ready_thread = all_threads[ready_id]; 	// got em!!
	ready_thread->status = RUNNING;

	free(exited_thread->stack);
	free(exited_thread);

	int err = setcontext(&(ready_thread->context));
	assert(!err);
}

int clear_queue(struct queue* queue) {
	if(queue->next == NULL)	{
		return 0; // queue is empty
	}
	
	struct queue* curr;
	struct queue* temp;

	curr = queue->next; // first node

	while(curr->next != NULL) {
		temp = curr;
		curr = curr->next; 
		free(temp);
	}

	free(curr);
	return 1;
}

Tid thread_kill(Tid tid) {
	//printf("so i am running %d and im tryna kill %d\n", thread_id(), tid);
	
	if(tid == thread_id())	return THREAD_INVALID;
	//printf("i am %d killing %d\n", thread_id(), tid);

	// lets clear the exit queue here maybe?
	//printf("exited queue before clear\n");
	//print_queue(exited_queue);
	
	//int clear = clear_queue(exited_queue);

	//if(clear == 0)
		//printf("queue was empty\n");
	//else
		//printf("deleted some stuff\n");

	//printf("print the queue\n");
	//print_queue(exited_queue);

	int valid_id = tid >= 0 && tid < THREAD_MAX_THREADS && tid != thread_id() && all_threads[tid] != NULL && all_threads[tid]->status == READY;
	if(!valid_id)	return THREAD_INVALID;
		
	// if this thread is in the ready queue then delete it from the ready queue
	struct thread* thread_to_kill = all_threads[tid];
	thread_to_kill->status = EXITED;
	delete_thread(ready_queue, tid);// delete this thread from the ready queue
	//print_queue(ready_queue);
	
	free(thread_to_kill->stack);
	free(thread_to_kill);
	all_threads[tid] = NULL;

	//printf("killing %d done\n\n", tid);
	return tid;
	
	
	return THREAD_FAILED;
}

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
