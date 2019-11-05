#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <pthread.h>

// global vars
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
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
};

/* static functions */
void worker (struct server *sv);
void send_to_buffer(struct server *sv, int connfd);
int read_from_buffer(struct server *sv);

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
	/* read file, 
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	ret = request_readfile(rq);
	if (ret == 0) { /* couldn't read file */
		goto out;
	}
	/* send file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
	file_data_free(data);
}

/* entry point functions */

struct server * server_init(int nr_threads, int max_requests, int max_cache_size) {
	struct server *sv;

	pthread_mutex_lock(&lock);

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
        
	
	sv->worker_threads = NULL;

    sv->buffer = NULL;
	sv->in = 0;
	sv->out = 0;
	sv->request_counter = 0;
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		if(max_requests > 0) 	/* Lab 4: create queue of max_request size when max_requests > 0 */
			sv->buffer = (int*)malloc((max_requests + 1) * sizeof(int));

		if(nr_threads > 0) { 	/* Lab 4: create worker threads when nr_threads > 0 */
			sv->worker_threads = (pthread_t**)malloc(nr_threads * sizeof(pthread_t*));
			for(int i = 0; i < nr_threads; i++) {
				sv->worker_threads[i] = (pthread_t*)malloc(sizeof(pthread_t));
				pthread_create(sv->worker_threads[i], NULL, (void*)&worker, (void*)sv);
			}
		}
		/* Lab 5: init server cache and limit its size to max_cache_size */
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

int read_from_buffer(struct server *sv) {
	int connfd = sv->buffer[sv->out]; // read the fd

	if (sv->request_counter == sv->max_requests) // if queue is full, broadcast to all workers waiting
		pthread_cond_broadcast(&full);

	sv->out = (sv->out + 1) % sv->max_requests; // increment out index 
	sv->request_counter--; // decrement buffer size
	return connfd;
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

void send_to_buffer(struct server *sv, int connfd) {
	sv->buffer[sv->in] = connfd; // add to buffer

	if(sv->request_counter == 0) // if it's empty
		pthread_cond_broadcast(&empty); // broadcast to all workers waiting for it not to be 

	sv->in = (sv->in+1) % sv->max_requests; // incremement in index
	sv->request_counter++; // size of buffer
}

void server_exit(struct server *sv) {
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */

	sv->exiting = 1;
	pthread_cond_broadcast(&empty);
	pthread_cond_broadcast(&full);
	
	for (int i = 0; i < sv->nr_threads; i++) {
		pthread_join(*(sv->worker_threads[i]), NULL);
		free(sv->worker_threads[i]);
	}

	if (sv->max_requests > 0) 
		free(sv->buffer);
	if (sv->nr_threads > 0) 
		free(sv->worker_threads);

	free(sv);
	return; 
}
