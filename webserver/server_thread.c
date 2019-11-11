#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <pthread.h>

// global vars
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t full = PTHREAD_COND_INITIALIZER; 
pthread_cond_t empty = PTHREAD_COND_INITIALIZER; 

struct server {
	int nr_threads; // # worker threads
	int max_requests; // buffer size
	int max_cache_size; // for lab 5
	int exiting; // ?
	pthread_t** worker_threads;

    int* buffer; // file descriptors are ints
	int in;
	int out;
	int request_counter;

	struct cache_struct *cache;
};

struct cache_struct {
	int curr_size;
	int max_size;
	int table_size;
	struct queue* queue;
	struct file_struct **table; // table with all the files
};

struct file_struct {
	char *key;
	struct file_data *data;
	int active;
};

struct queue {
    int index;
    struct queue* next;
};

/* static functions */
void worker (struct server *sv);
void send_to_buffer(struct server *sv, int connfd);
int read_from_buffer(struct server *sv);

struct file_struct* cache_lookup(struct server *sv, char * file_name);
struct file_struct* cache_insert(struct server *sv, struct file_data *data);
int cache_evict(struct server *sv, int file_size);

unsigned long get_index(struct server *sv, char *word_string);
struct file_struct* table_insert(struct server *sv, struct file_data *data);

void LRU_add(struct server *sv, int index);
void LRU_update(struct server *sv, int index); // delete from any point
int LRU_delete(struct server *sv, int file_size); // delete from the end

void add_to_front(struct queue* queue, int index);
void delete_from_queue(struct queue* queue, int index);
void add_to_end(struct queue* queue, int index);

void server_basic_init(struct server *sv, int nr_threads, int max_requests, int max_cache_size);
void cache_init(struct server *sv, int max_cache_size);

void free_cache_element(struct file_struct *file);

void server_basic_init(struct server *sv, int nr_threads, int max_requests, int max_cache_size) {
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
	sv->worker_threads = NULL;
	sv->cache = NULL;
    sv->buffer = NULL;
	sv->in = 0;
	sv->out = 0;
	sv->request_counter = 0;
}

void cache_init(struct server *sv, int max_cache_size) {
	sv->cache = (struct cache_struct*)malloc(sizeof(struct cache_struct));
	sv->cache->table_size = 2147000;
	sv->cache->table = (struct file_struct**)malloc(sv->cache->table_size * sizeof(struct file_struct*));
	sv->cache->queue = (struct queue*)malloc(sizeof(struct queue));
	
	sv->cache->curr_size = 0;
	sv->cache->max_size = max_cache_size;
	sv->cache->queue->next = NULL;
	for(int i = 0; i < sv->cache->table_size; i++) {
		sv->cache->table[i] = NULL;
	}
}

/* initialize file data */
static struct file_data * file_data_init(void) {
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void file_data_free(struct file_data *data) {
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

void free_cache_element(struct file_struct *file) {
	free(file->key);
	file_data_free(file->data);
}

void send_to_buffer(struct server *sv, int connfd) {
	sv->buffer[sv->in] = connfd; // add to buffer

	if(sv->request_counter == 0) // if it's empty
		pthread_cond_broadcast(&empty); // broadcast to all workers waiting for it not to be 

	sv->in = (sv->in+1) % sv->max_requests; // incremement in index
	sv->request_counter++; // size of buffer
}

int read_from_buffer(struct server *sv) {
	int connfd = sv->buffer[sv->out]; // read the fd

	if (sv->request_counter == sv->max_requests) // if queue is full, broadcast to all workers waiting
		pthread_cond_broadcast(&full);

	sv->out = (sv->out + 1) % sv->max_requests; // increment out index 
	sv->request_counter--; // decrement buffer size
	return connfd;
}

// adds it to the start of queue
void add_to_front(struct queue* queue, int index) {
    struct queue* head;
    struct queue* temp;
    struct queue* new;
	
    if(queue == NULL) 		return; 
    
    head = queue;

    new  = (struct queue*)malloc(sizeof(struct queue));
    new->index = index;
    new->next = NULL;
    
    if(queue->next == NULL) 
        queue->next = new;
    else { 
		temp = head;
		while(temp->next != NULL) 
		    temp = temp->next;
		temp->next = new;
		return;
    }
}

// adds it to the end of queue
void add_to_end(struct queue* queue, int index) {
	struct queue* curr;
	struct queue* new;

    // if(queue == NULL) 	return; 			// no thread in ready queue
	new  = (struct queue*)malloc(sizeof(struct queue));
	new->index = index;
    new->next = NULL;

	if(queue->next == NULL) {
		queue->next = new;
		return;
	}

    curr = queue->next; // first element of the list

    while(curr->next != NULL) {
        curr = curr->next;
    }

	curr->next = new;
}

// delete from any point in the queue
void delete_from_queue(struct queue* queue, int index) {
    struct queue* curr;
    struct queue* prev; 

    if(queue == NULL || queue->next == NULL) 	return; 			// no thread in ready queue

    prev = queue;
    curr = queue->next; // first element of the list

    while(curr->index != index && curr->next != NULL) {
        prev = curr;
        curr = curr->next;
    }

    if(curr->index == index) {
        prev->next = curr->next;
        free(curr);
        curr = NULL;
    }
}

// hash function: djb2. author: dan bernstein. source: http://www.cse.yorku.ca/~oz/hash.html
unsigned long get_index(struct server *sv, char *word_string) {
    unsigned long hash = 5381;
    int c;
    
    while ((c = *word_string++) != '\0')
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash % sv->cache->table_size;
}

// returns file from cache table if file_name is found in struct, otherwise NULL
struct file_struct* cache_lookup(struct server *sv, char * file_name) {
	int index = get_index(sv, file_name); // get index of the file_name

	// look for the file in cache table
	while(sv->cache->table != NULL && sv->cache->table[index] != NULL && strcmp(sv->cache->table[index]->key, file_name) != 0) {
        index++;
        index %= sv->cache->table_size;
    }

	// data found
	if(sv->cache->table[index] != NULL && strcmp(sv->cache->table[index]->key, file_name) == 0)
		return sv->cache->table[index];

	return NULL;
}

// inserts file into queue if it isn't already in it and there can be space for it. otherwise returns NULL.
struct file_struct* cache_insert(struct server *sv, struct file_data *data) {
	// file already in cache, so return it
	if(cache_lookup(sv, data->file_name) != NULL)
		return cache_lookup(sv, data->file_name); 

	// try to acquire enough room for the file. if successful, insert the file into the cache
	if(cache_evict(sv, data->file_size) == 1) { 
		return table_insert(sv, data);
	}

	// not enough room in cache for file
	return NULL;
}

// returns 1 if enough space for file. returns 0 otherwise.
int cache_evict(struct server *sv, int file_size) {
	// file size is greater than max possible size
	if(file_size > sv->cache->max_size)
		return 0; 
	
	// already enough room for the file
	if(file_size <= sv->cache->max_size - sv->cache->curr_size)
		return 1; 

	// nothing in LRU queue so nothing to delete.
	if(sv->cache->queue->next == NULL) 
		return 0;

	return LRU_delete(sv, file_size); // returns success or failure 
}

// keeps deleting files according to the LRU
int LRU_delete(struct server *sv, int file_size) { 
	struct queue* curr;
	struct cache_struct *cache = sv->cache;

	curr = cache->queue->next; 

	while(curr != NULL && (cache->max_size - cache->curr_size) < file_size) {
		struct file_struct *file = cache->table[curr->index];
		if(file != NULL && file->active == 0) {
			cache->curr_size -= file->data->file_size;
			free_cache_element(cache->table[curr->index]);
			cache->table[curr->index] = NULL;
			delete_from_queue(cache->queue, curr->index);
		}
		curr = curr->next;
	}	
	
	// now enough space for file
	if((cache->max_size - cache->curr_size) >= file_size)
		return 1;

	// could not acquire enough space for file
	return 0;
}

struct file_struct* table_insert(struct server *sv, struct file_data *data) {
	struct file_struct* file_struct;
	unsigned long index;

    // get index (hash) of word in hash_array
    index = get_index(sv, data->file_name);
    
    // avoiding collisions and repeated words
    while(sv->cache->table != NULL && sv->cache->table[index] != NULL && strcmp(sv->cache->table[index]->key, data->file_name) != 0) {
        index++;
        index%=sv->cache->table_size;
    }
    
	file_struct = (struct file_struct*)malloc(sizeof(struct file_struct));

	file_struct->key = (char *)malloc((strlen(data->file_name) + 1) * sizeof(char)); // is strlen allowed to be in here
	strcpy(file_struct->key, data->file_name); // not allowed to do direct string assignment
	file_struct->key[strlen(data->file_name)] = '\0';

	file_struct->data = data;
	file_struct->active = 0;

	sv->cache->table[index] = file_struct;
	sv->cache->curr_size += data->file_size;
	LRU_add(sv, index);
	return sv->cache->table[index];
}

// add to the front of the LRU
void LRU_add(struct server *sv, int index) {
	add_to_front(sv->cache->queue, index); // least recently used
}

// delete index from any point in LRU and add it to the start
void LRU_update(struct server *sv, int index) {
	delete_from_queue(sv->cache->queue, index);
	add_to_end(sv->cache->queue, index); // end is the MRU (fingers crossed)
}
 
static void do_server_request(struct server *sv, int connfd) {
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}

	if(sv->max_cache_size > 0) {
		pthread_mutex_lock(&cache_lock);
		struct file_struct *file = cache_lookup(sv, data->file_name); 		// is the file name in the cache
		if(file != NULL) { // gotem
			printf("**cache HIT**: %s\n", file->key);
			data = file->data; // fetch data from file
			request_set_data(rq, file->data); // update rq->data = data
			if(file != NULL) file->active++;
			LRU_update(sv, get_index(sv, data->file_name));
			pthread_mutex_unlock(&cache_lock);

			request_sendfile(rq); // send file

			pthread_mutex_lock(&cache_lock);
			if(file != NULL) file->active--;
			pthread_mutex_unlock(&cache_lock);

			goto out; // done, go to out
		}
		else if(file == NULL) { // not in cache
			printf("**cache MISS**: %s\n", data->file_name);
			pthread_mutex_unlock(&cache_lock);

			ret = request_readfile(rq);
			if (ret == 0)	goto out; /* couldn't read file */

			pthread_mutex_lock(&cache_lock);
			file = cache_insert(sv, data); // only if it can fit but i guess the check can be done in here
			request_set_data(rq, data);
			if(file != NULL) {
				file->active++;
				LRU_update(sv, get_index(sv, data->file_name));
			}
			pthread_mutex_unlock(&cache_lock);

			request_sendfile(rq);

			pthread_mutex_lock(&cache_lock);
			if(file != NULL) file->active--;
			pthread_mutex_unlock(&cache_lock);

			goto out;
		}
	}
	else { // no cache
		/* read file, 
		* fills data->file_buf with the file contents,
		* data->file_size with file size. */
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		}
		/* send file to client */
		request_sendfile(rq);
	}
out:
	request_destroy(rq);
	// file_data_free(data); // LOL THIS IS THE PROBLEM
} 

/* entry point functions */
struct server * server_init(int nr_threads, int max_requests, int max_cache_size) {
	struct server *sv;
	pthread_mutex_lock(&lock); // UPDATE THIS

	sv = Malloc(sizeof(struct server));
	server_basic_init(sv, nr_threads, max_requests, max_cache_size);
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		if(max_requests > 0) 	
			sv->buffer = (int*)malloc((max_requests + 1) * sizeof(int));

		if(nr_threads > 0) { 	
			sv->worker_threads = (pthread_t**)malloc(nr_threads * sizeof(pthread_t*));
			for(int i = 0; i < nr_threads; i++) {
				sv->worker_threads[i] = (pthread_t*)malloc(sizeof(pthread_t));
				pthread_create(sv->worker_threads[i], NULL, (void*)&worker, (void*)sv);
			}
		}

		if(max_cache_size > 0) {
			cache_init(sv, max_cache_size);
		}
	}

	pthread_mutex_unlock(&lock);
	return sv;
}

void worker (struct server *sv) {
	while(1) {
		pthread_mutex_lock(&lock);
		while(sv->request_counter == 0) { // if its empty, wait
			pthread_cond_wait(&empty, &lock);       
			if(sv->exiting) {
				pthread_mutex_unlock(&lock);
				pthread_exit(NULL);
			}
		}
		int connfd = read_from_buffer(sv);
		pthread_mutex_unlock(&lock);
		do_server_request(sv, connfd); // process request
	}
}

// connfd is the socket descriptor the server will use to send data to the client
void server_request(struct server *sv, int connfd) {
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
    }
	else {
		pthread_mutex_lock(&lock);
		while (sv->request_counter == sv->max_requests) {// buffer full 
			pthread_cond_wait(&full, &lock);
			if(sv->exiting) {
				pthread_mutex_unlock(&lock);
				pthread_exit(NULL);
			}
		} 
		send_to_buffer(sv, connfd);
		pthread_mutex_unlock(&lock);
    }
}

void server_exit(struct server *sv) {
	sv->exiting = 1;
	pthread_cond_broadcast(&empty);
	pthread_cond_broadcast(&full);
	
	
	for (int i = 0; i < sv->nr_threads; i++) {
		pthread_join(*(sv->worker_threads[i]), NULL);
		free(sv->worker_threads[i]);
	}

	if (sv->max_requests > 0) {
		free(sv->buffer);
	}
	if (sv->nr_threads > 0) {
		free(sv->worker_threads);
	}
	if(sv->max_cache_size > 0) {
		// delete everything in the cache table
		for(int i = 0; i < sv->cache->table_size; i++) {
			if(sv->cache->table[i] != NULL) {
				free_cache_element(sv->cache->table[i]);
				sv->cache->table[i] = NULL;
			}
		}
		
		// delete everything in the LRU queue
		struct queue* current = sv->cache->queue->next;  
		struct queue* next;  
		
		while (current != NULL) {  
			next = current->next;  
			free(current);  
			current = next;  
		}  
			
		sv->cache->queue = NULL; 
		free(sv->cache->queue);
		free(sv->cache->table);
		free(sv->cache);
	}

	free(sv);
	return; 
}

