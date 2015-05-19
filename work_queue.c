/** @file
 * Implementation of queue for executing tasks in parallel.
 *
 *==============================================================================
 * Copyright 2015 by Brandon Edens. All Rights Reserved
 *==============================================================================
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @author  Brandon Edens
 * @date    2015-05-18
 * @details
 *
 */

/*******************************************************************************
 * Include Files
 */

#include "work_queue.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <sched.h>


/*******************************************************************************
 * Local Types
 */

/** A worker that executes jobs. */
struct worker {
	/** Thread of execution. */
	pthread_t thread;

	/** Flag that terminates the thread. */
	bool term;

	/** Queue of available work for this worker. */
	struct work_queue *queue;

	/** The next worker available in the list of workers. */
	struct worker *next;
};

/** Definition of a unit of work that contains a function that will be invoked
 * on a thread of execution.
 */
struct job {
	/** Function to execute for this job. */
	void (*func)(void *);

	/** Data to pass to the function that executes for this job. */
	void *data;

	/** The previous job in the list of jobs. */
	struct job *prev;

	/** The next job in the list of jobs. */
	struct job *next;
};

/** A queue of workers that invoke the collection of pending jobs. */
struct work_queue {
	/** The list of workers available for executing jobs. */
	struct worker *workers;

	/** The list of pending jobs to execute. */
	struct job *pending_jobs;

	/** Mutex used to manage the pending jobs. */
	pthread_mutex_t jobs_mutex;

	/** Condition used to direct execution of jobs or shutdown workers. */
	pthread_cond_t jobs_cond;
};

/*******************************************************************************
 * Local Functions
 */

static void *exec_worker(void *data);
static struct job *job_alloc(void);
static void job_free(struct job *job);

/******************************************************************************/

/** Execute a worker. */
static void *exec_worker(void *data)
{
	struct worker *w = (struct worker *)data;

	while (true) {
		pthread_mutex_lock(&w->queue->jobs_mutex);
		if (w->term) {
			goto cleanup;
		}
		pthread_cond_wait(&w->queue->jobs_cond, &w->queue->jobs_mutex);
		if (w->term) {
			goto cleanup;
		}

		// We're not terminating so we're going to process a job.
		assert(NULL != w->queue->pending_jobs);
		// Extract a job from the queue. */
		struct work_queue *queue = w->queue;
		struct job *j = queue->pending_jobs;
		if (NULL != j) {
			if (NULL != j->prev) {
				j->prev->next = j->next;
			}
			if (NULL != j->next) {
				j->next->prev = j->prev;
			}
			if (queue->pending_jobs == j) {
				queue->pending_jobs = j->next;
			}
		}
		pthread_mutex_unlock(&queue->jobs_mutex);

		j->func(j->data);
		// Delete resources associated with the job.
		job_free(j);
	}

cleanup:
	pthread_mutex_unlock(&w->queue->jobs_mutex);
	return NULL;
}

/** Allocate memory for a job. */
static struct job *job_alloc(void)
{
	return calloc(1, sizeof(struct job));
}

/** Free the memory associated with a job. */
static void job_free(struct job *job)
{
	free(job);
}

/** Add to the work queue the given function / data to be executed. */
void work_queue_add(struct work_queue *queue, void (*func)(void *), void *data)
{
	struct job *job = job_alloc();
	job->func = func;
	job->data = data;

	pthread_mutex_lock(&queue->jobs_mutex);
	job->next = queue->pending_jobs;
	queue->pending_jobs = job;
	pthread_cond_signal(&queue->jobs_cond);
	pthread_mutex_unlock(&queue->jobs_mutex);
}

/** Allocate resources associated with a work queue. */
struct work_queue *work_queue_alloc(void)
{
	return calloc(1, sizeof(struct work_queue));
}

/** Free the resources associated with a work queue. */
void work_queue_free(struct work_queue *queue)
{
	free(queue);
}

/** Initialize the work queue with the given number of workers. */
int work_queue_init(struct work_queue *queue, int num_workers)
{
	pthread_mutex_init(&queue->jobs_mutex, NULL);
	pthread_cond_init(&queue->jobs_cond, NULL);

	for (int i = 0; i < num_workers; i++) {
		struct worker *worker = calloc(1, sizeof(struct worker));
		worker->queue = queue;
		if (0 != pthread_create(&worker->thread, NULL, exec_worker,
		                        (void *)worker)) {
			fprintf(stderr, "Failure to worker thread: %p.",
					(void *)worker);
			return 1;
		}
		worker->next = queue->workers;
		queue->workers = worker;
	}

	return 0;
}

/** Terminate execution of the work queue. */
void work_queue_term(struct work_queue *queue)
{
	for (struct worker *w = queue->workers; NULL != w; w = w->next) {
		w->term = true;
	}

	// Attempt to wake all threads.
	pthread_mutex_lock(&queue->jobs_mutex);
	pthread_cond_broadcast(&queue->jobs_cond);
	pthread_mutex_unlock(&queue->jobs_mutex);

	// Wait for the threads to terminate.
	for (struct worker *w = queue->workers; NULL != w; w = w->next) {
		void *ret;
		pthread_join(w->thread, &ret);
	}

	pthread_mutex_lock(&queue->jobs_mutex);
	// Clear out all created workers.
	struct worker *w = queue->workers;
	while (NULL != w) {
		struct worker *w_next = w->next;
		free(w);
		w = w_next;
	}

	struct job *j = queue->pending_jobs;
	while (NULL != j) {
		struct job *j_next = j->next;
		job_free(j);
		j = j_next;
	}
	queue->workers = NULL;
	queue->pending_jobs = NULL;
	pthread_mutex_unlock(&queue->jobs_mutex);

	pthread_cond_destroy(&queue->jobs_cond);
	pthread_mutex_destroy(&queue->jobs_mutex);
}
