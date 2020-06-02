

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

typedef struct {
	size_t size;
	void* (*ctor)(void *_self, va_list *params);
	void* (*dtor)(void *_self);
} AbstractClass;

#define LL_ADD(item, list) do { 	\
	item->prev = NULL;				\
	item->next = list;				\
	list = item;					\
} while(0)

#define LL_REMOVE(item, list) do {						\
	if (item->prev != NULL) item->prev->next = item->next;	\
	if (item->next != NULL) item->next->prev = item->prev;	\
	if (list == item) list = item->next;					\
	item->prev = item->next = NULL;							\
} while(0)


typedef struct NWORKER {
	pthread_t thread;
	int terminate;
	struct NWORKQUEUE *workqueue;
	struct NWORKER *prev;
	struct NWORKER *next;
} nWorker;

typedef struct NJOB {
	void (*job_function)(struct NJOB *job);
	void *user_data;
	struct NJOB *prev;
	struct NJOB *next;
} nJob;

typedef struct NWORKQUEUE {
	struct NWORKER *workers;
	struct NJOB *waiting_jobs;
	pthread_mutex_t jobs_mtx;
	pthread_cond_t jobs_cond;
 	int minimum;
 	int maximum;
	int idel;
	int threadsum;
	unsigned int linger;
} nWorkQueue;
static void* ntyWorkerThread(void *arg);
typedef nWorkQueue nThreadPool;
static int ntyWorkerCreate(nThreadPool *workqueue) {

    nWorker *worker = (nWorker*)malloc(sizeof(nWorker));
    if (worker == NULL) {
        perror("malloc");
        return 1;
    }

    memset(worker, 0, sizeof(nWorker));
    worker->workqueue = workqueue;

    //printf("pthread_create --> %d\n", i);
    int ret = pthread_create(&worker->thread, NULL, ntyWorkerThread, (void *)worker);
    if (ret) {
        
        perror("pthread_create");
        free(worker);

        return 1;
    }

    LL_ADD(worker, worker->workqueue->workers);

	return ret;
}

static void *ntyWorkerThread(void *ptr) {

	struct timespec ts;
	nWorker *worker = (nWorker*)ptr;

	while (1) {
		pthread_mutex_lock(&worker->workqueue->jobs_mtx);

		worker->workqueue->idel++;
		while (worker->workqueue->waiting_jobs == NULL) {
			if (worker->terminate) break;

			if (worker->workqueue->threadsum <= worker->workqueue->minimum) {
				pthread_cond_wait(&worker->workqueue->jobs_cond, &worker->workqueue->jobs_mtx);
			}
			else
			{
				clock_gettime(CLOCK_REALTIME, &ts);
				ts.tv_sec += worker->workqueue->linger;

				if (worker->workqueue->linger == 0 || pthread_cond_timedwait(&worker->workqueue->jobs_cond, &worker->workqueue->jobs_mtx, &ts) == ETIMEDOUT) {
					worker->terminate = 1;
					break;
				}		
			}
			
		}
		worker->workqueue->idel--;

		if (worker->terminate) {
			worker->workqueue->threadsum--;
			pthread_mutex_unlock(&worker->workqueue->jobs_mtx);
			break;
		}
		
		nJob *job = worker->workqueue->waiting_jobs;
		if (job != NULL) {
			LL_REMOVE(job, worker->workqueue->waiting_jobs);
		}
		
		pthread_mutex_unlock(&worker->workqueue->jobs_mtx);

		if (job == NULL) continue;

		job->job_function(job);
	}

	free(worker);
	pthread_exit(NULL);
}


int ntyThreadPoolCreate(nThreadPool *workqueue, int min_threads, int max_threads,int linger) {

	memset(workqueue, 0, sizeof(nThreadPool));
	
	pthread_cond_t blank_cond = PTHREAD_COND_INITIALIZER;
	memcpy(&workqueue->jobs_cond, &blank_cond, sizeof(workqueue->jobs_cond));
	
	pthread_mutex_t blank_mutex = PTHREAD_MUTEX_INITIALIZER;
	memcpy(&workqueue->jobs_mtx, &blank_mutex, sizeof(workqueue->jobs_mtx));

	workqueue->linger = linger;
	workqueue->minimum = min_threads;
	workqueue->maximum = max_threads;
	
	return 0;
}


void ntyThreadPoolShutdown(nThreadPool *workqueue) {
	nWorker *worker = NULL;

	for (worker = workqueue->workers;worker != NULL;worker = worker->next) {
		worker->terminate = 1;
	}

	pthread_mutex_lock(&workqueue->jobs_mtx);

	workqueue->workers = NULL;
	workqueue->waiting_jobs = NULL;

	pthread_cond_broadcast(&workqueue->jobs_cond);

	pthread_mutex_unlock(&workqueue->jobs_mtx);
	
}

void ntyThreadPoolQueue(nThreadPool *workqueue, nJob *job) {

	pthread_mutex_lock(&workqueue->jobs_mtx);

	LL_ADD(job, workqueue->waiting_jobs);
	
	if(workqueue->idel>0)
	{
		pthread_cond_signal(&workqueue->jobs_cond);
	} else if (workqueue->threadsum < workqueue->maximum && ntyWorkerCreate(workqueue) == 0) {
		workqueue->threadsum ++;
	}

	pthread_mutex_unlock(&workqueue->jobs_mtx);
	
}

#if 0

#define KING_MAX_THREAD			80
#define KING_COUNTER_SIZE		1000

void king_counter(nJob *job) {

	int index = *(int*)job->user_data;

	printf("index : %d, selfid : %lu\n", index, pthread_self());
	
	free(job->user_data);
	free(job);
}

int main(int argc, char *argv[]) {

	nThreadPool pool;

	ntyThreadPoolCreate(&pool, KING_MAX_THREAD);
	
	int i = 0;
	for (i = 0;i < KING_COUNTER_SIZE;i ++) {
		nJob *job = (nJob*)malloc(sizeof(nJob));
		if (job == NULL) {
			perror("malloc");
			exit(1);
		}
		
		job->job_function = king_counter;
		job->user_data = malloc(sizeof(int));
		*(int*)job->user_data = i;

		ntyThreadPoolQueue(&pool, job);
		
	}

	getchar();
	printf("\n");

	
}
#endif 

void *New(const void *_class, ...) {
	const AbstractClass *class = _class;
	void *p = calloc(1, class->size);
	memset(p, 0, class->size);
	
	assert(p);
	*(const AbstractClass**)p = class;
	
	if (class->ctor) {
		va_list params;
		va_start(params, _class);
		p = class->ctor(p, &params);
		va_end(params);
	}
	return p;
}


void Delete(void *_class) {
	const AbstractClass **class = _class;

	if (_class && (*class) && (*class)->dtor) {
		_class = (*class)->dtor(_class);
	}
	
	free(_class);
}



typedef struct _ThreadPool {
    const void *_;
    nThreadPool *wq;
} ThreadPool;

typedef struct _ThreadPoolOpera {

    size_t size;
    void* (*ctor)(void *_self, va_list *params);
	void* (*dtor)(void *_self);
	void (*addJob)(void *_self, void *task);

} ThreadPoolOpera;



void* ntyThreadPoolCtor(void *_self, va_list *params) {
	ThreadPool *pool = (ThreadPool*)_self;

	pool->wq = (nThreadPool*)malloc(sizeof(nThreadPool));

	pool->wq->workers = malloc(sizeof(nWorker));
	memset(pool->wq->workers, 0, sizeof(nWorker));
	pool->wq->waiting_jobs = malloc(sizeof(nJob));
	memset(pool->wq->waiting_jobs, 0, sizeof(nJob));


	int arg1 = va_arg(params, int);
	int arg2 = va_arg(params, int);
	int arg3 = va_arg(params, int);

    printf("ntyThreadPoolCtor --> %d %d %d\n", arg1,arg2,arg3);
    ntyThreadPoolCreate(pool->wq, arg1,arg2,arg3);
#if 0
	if (arg == 1) {
		ntyWorkQueueInit(pool->wq, NTY_THREAD_POOL_NUM / 2);
	} else {
		ntyWorkQueueInit(pool->wq, NTY_THREAD_POOL_NUM);
	}
#endif

	return pool;
}

void* ntyThreadPoolDtor(void *_self) {
	ThreadPool *pool = (ThreadPool*)_self;

	ntyThreadPoolShutdown(pool->wq);
	free(pool->wq);

	return pool;
}


void ntyThreadPoolAddJob(void *_self, void *task) {
	ThreadPool *pool = (ThreadPool*)_self;
	nJob *job = (nJob*)task;
	
	ntyThreadPoolQueue(pool->wq, job);
}

const ThreadPoolOpera ntyThreadPoolOpera = {
	sizeof(ThreadPool),
	ntyThreadPoolCtor,
	ntyThreadPoolDtor,
	ntyThreadPoolAddJob,
};

const void *pNtyThreadPoolOpera = &ntyThreadPoolOpera;
static void *pThreadPool = NULL;

void *ntyThreadPoolInstance(int min_threads, int max_threads,int linger) {
	if (min_threads > max_threads || max_threads < 1) {
		errno = EINVAL;
		return NULL;
	}

    if (pThreadPool == NULL) {
        pThreadPool = New(pNtyThreadPoolOpera,min_threads,max_threads,linger); 
    }

    return pThreadPool; 
}

void* ntyGetInstance(void) {
    if (pThreadPool != NULL) return pThreadPool;
}

void ntyThreadPoolRelease(void) {	
	Delete(pThreadPool);
	pThreadPool = NULL;
}

int ntyThreadPoolPush(void *self, void *task) {
	ThreadPoolOpera **pThreadPoolOpera = self;

	if (self && (*pThreadPoolOpera) && (*pThreadPoolOpera)->addJob) {
		(*pThreadPoolOpera)->addJob(self, task);
		return 0;
	}
	return 1;
}

#if 1

#define KING_COUNTER_SIZE		100000

void king_counter(nJob *job) {

	int index = *(int*)job->user_data;

	printf("index : %d, selfid : %lu\n", index, pthread_self());
	
	free(job->user_data);
	free(job);
}


int main(int argc, char *argv[]) {

	
    void *pool = ntyThreadPoolInstance(10,20,15);

    int i = 0;
    for (i = 0;i < KING_COUNTER_SIZE;i ++) {
		nJob *job = (nJob*)malloc(sizeof(nJob));
		if (job == NULL) {
			perror("malloc");
			exit(1);
		}
		
		job->job_function = king_counter;
		job->user_data = malloc(sizeof(int));
		*(int*)job->user_data = i;

		ntyThreadPoolPush(pool, job);
		
	}

	getchar();
	printf("\n");

	
}

#endif
