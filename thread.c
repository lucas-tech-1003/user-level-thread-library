#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdio.h>
#include <signal.h>
#include "thread.h"
#include "malloc369.h"
#include "interrupt.h"

#define READY 0  /* thread is ready */
#define SLEEPING 1  /* thread is sleeping */
#define KILLED  2  /* thread is killed */
#define EXITED  3  /* thread is exited */

/* For Assignment 1, you will need a queue structure to keep track of the 
 * runnable threads. You can use the tutorial 1 queue implementation if you 
 * like. You will probably find in Assignment 2 that the operations needed
 * for the wait_queue are the same as those needed for the ready_queue.
 */

/* This is the thread control block. */
struct thread {
	Tid tid;
	int exit_code_from_parent;
	int exit_status;
	Tid yield_to_tid;
	Tid wait_to_tid;
	int thread_state; // 2: KILLED, 3: EXITED
	ucontext_t thread_context; 
	void *thread_stack_pointer; // pointer to the original malloced thread stack
	struct wait_queue *in_wq; // pointer to the queue waiting on
	struct wait_queue *wq; // pointer to the wait queue
};

/* This is the ready queue structure. */
typedef struct q_node {
	struct thread *thread;
} q_node_t;

/* This is the wait queue structure, needed for Assignment 2. */ 
struct wait_queue {
	int wq_size;
	q_node_t waiting_queue[THREAD_MAX_THREADS];
	q_node_t *wq_head;
	q_node_t *wq_tail;
};

q_node_t ready_queue[THREAD_MAX_THREADS];
q_node_t zombie_queue[THREAD_MAX_THREADS];
q_node_t *zombie_queue_head = NULL;
q_node_t *zombie_queue_tail = NULL;
q_node_t *rqueue_head = NULL;
q_node_t *rqueue_tail = NULL;
int num_threads;
int rq_size; // size of the ready queue.
int zq_size; // size of the zombie queue.

/* Global variables*/
int available_tids[THREAD_MAX_THREADS];	// array of available tid. 0 means available, otherwise 1.
struct thread *running_thread = NULL;  // pointer to the running thread.

/* Helper functions */
int ready_queue_enqueue(struct thread *new_thread) {
	if (rq_size == THREAD_MAX_THREADS) {
		return 0;
	}
	rqueue_tail->thread = new_thread;
	rqueue_tail = &ready_queue[(rqueue_tail - ready_queue + 1) % THREAD_MAX_THREADS];
	rq_size++;
	return 1;
}


int ready_queue_dequeue(struct thread **next_thread) {
	if (rq_size == 0) {
		return 0;
	} // empty queue
	if (next_thread == NULL) {
		return 0;
	}
	*next_thread = rqueue_head->thread;
	rqueue_head->thread = NULL;
	rqueue_head = &ready_queue[(rqueue_head - ready_queue + 1) % THREAD_MAX_THREADS];
	rq_size--;
	return 1;
}


int remove_from_ready_queue(Tid tid, struct thread **next_thread) {
	if (rq_size == 0) {
		return 0;
	}
	if (rqueue_head->thread->tid == tid) {
		struct thread *temp = rqueue_head->thread;
		*next_thread = temp;
		rqueue_head->thread = NULL;
		assert(*next_thread != NULL);
		rq_size--;
		rqueue_head = &ready_queue[(rqueue_head - ready_queue + 1) % THREAD_MAX_THREADS];
		return 1;
	}
	assert(rqueue_tail->thread == NULL);
	q_node_t *curr = rqueue_head;
	q_node_t *prev = NULL;
	while (curr != rqueue_tail) {
		if (curr->thread->tid == tid) {
			struct thread *temp = curr->thread;
			*next_thread = temp;
			curr->thread = NULL;
			assert(*next_thread != NULL);
			rq_size--;
			// shift nodes in the queue
			while (curr != rqueue_tail) {
				q_node_t *next = &ready_queue[(curr - ready_queue + 1) % THREAD_MAX_THREADS];
				curr->thread = next->thread;
				prev = curr;
				curr = next;
			}

			// Clear the last node and update the tail
			curr->thread = NULL;
			rqueue_tail = prev;
			assert(rqueue_tail->thread == NULL);
			return 1;
		}
		prev = curr;
		curr = &ready_queue[(curr - ready_queue + 1) % THREAD_MAX_THREADS];
	}
	// Not found
	return 0;
}


int wait_queue_enqueue(struct wait_queue *wq, struct thread *new_thread) {
	if (wq->wq_size == THREAD_MAX_THREADS) {
		return 0;
	}
	wq->wq_tail->thread = new_thread;
	wq->wq_tail = &wq->waiting_queue[(wq->wq_tail - wq->waiting_queue + 1) % THREAD_MAX_THREADS];
	wq->wq_size++;
	return 1;
}


struct thread *wait_queue_dequeue(struct wait_queue *wq) {
	struct thread *return_thread = NULL;
	if (wq->wq_size == 0) {
		return return_thread;
	}
	return_thread = wq->wq_head->thread;
	wq->wq_head->thread = NULL;
	wq->wq_head = &wq->waiting_queue[(wq->wq_head - wq->waiting_queue + 1) % THREAD_MAX_THREADS];
	wq->wq_size--;
	return return_thread;
}


struct thread *wait_queue_remove(struct wait_queue *wq, Tid tid) {
	struct thread *return_thread = NULL;
	if (wq->wq_size == 0) {
		return return_thread;
	}
	// search through wait queue and remove
	if (wq->wq_head->thread->tid == tid) {
		return_thread = wq->wq_head->thread;
		wq->wq_head->thread = NULL;
		wq->wq_size--;
		wq->wq_head = &wq->waiting_queue[(wq->wq_head - wq->waiting_queue + 1) % THREAD_MAX_THREADS];
		return return_thread;
	}
	q_node_t *curr = wq->wq_head;
	q_node_t *prev = NULL;
	while (curr != wq->wq_tail) {
		if (curr->thread->tid == tid) {
			return_thread = curr->thread;
			curr->thread = NULL;
			wq->wq_size--;
			// shift nodes in the queue
			while (curr != wq->wq_tail) {
				q_node_t *next = &wq->waiting_queue[(curr - wq->waiting_queue + 1) % THREAD_MAX_THREADS];
				curr->thread = next->thread;
				prev = curr;
				curr = next;
			}

			// Clear the last node and update the tail
			curr->thread = NULL;
			wq->wq_tail = prev;
			assert(wq->wq_tail->thread == NULL);
			return return_thread;
		}
		prev = curr;
		curr = &wq->waiting_queue[(curr - wq->waiting_queue + 1) % THREAD_MAX_THREADS];
	}

	return return_thread;
}


int zombie_queue_enqueue(struct thread *new_thread) {
	if (zq_size == THREAD_MAX_THREADS) {
		return 0;
	}
	if (zq_size == 0 && zombie_queue_head == NULL) {
		zombie_queue_head->thread = new_thread;
	}
	zombie_queue_tail->thread = new_thread;
	zombie_queue_tail = &zombie_queue[(zombie_queue_tail - zombie_queue + 1) % THREAD_MAX_THREADS];
	zq_size++;
	return 1;
}

void destroy_zombie_queue() {
	if (zq_size == 0) {
		return;
	}
	q_node_t *curr = zombie_queue_head;
	while (curr->thread != NULL) {
		struct thread *temp = curr->thread;
		available_tids[temp->tid] = 0;
		num_threads--;
		zq_size--;
		wait_queue_destroy(temp->wq);
		free369(temp->thread_stack_pointer);
		free369(temp);

		curr->thread = NULL;

		curr = &zombie_queue[(curr - zombie_queue + 1) % THREAD_MAX_THREADS];
	}
	zombie_queue_head = zombie_queue_tail = zombie_queue;
	assert(zq_size == 0);
	assert(zombie_queue_head->thread == NULL);
	assert(zombie_queue_tail->thread == NULL);
	return;
}


int swapcontext_t(ucontext_t *cctx, ucontext_t *nctx) {
    volatile int count = 0;

    int result = getcontext(cctx);
    if (result == 0 && !count) {
        count = 1;
        result = setcontext(nctx);
    }

    return result;
}

/**************************************************************************
 * Assignment 1: Refer to thread.h for the detailed descriptions of the six
 *               functions you need to implement. 
 **************************************************************************/

/* Add necessary initialization for your threads library here. */
void
thread_init(void)
{
	/* Initialize the ready queue */
	rqueue_head = rqueue_tail = ready_queue;
	rq_size = 0;
	/* Initialize the zombie queue */
	zombie_queue_head = zombie_queue_tail = zombie_queue;
	zq_size = 0;
    /* Initialize the thread control block for the first thread */
	struct thread *first_thread = malloc369(sizeof(struct thread));
	if (!first_thread) {
		printf("malloc failed\n");
		exit(1);
	}
	first_thread->wq = wait_queue_create();
	assert(first_thread->wq);
	first_thread->in_wq = NULL;
	first_thread->exit_code_from_parent = -999;
	first_thread->tid = 0;
	available_tids[0] = 1;
	int err = getcontext(&first_thread->thread_context); // initialize the context
	assert(err == 0);

	num_threads = 1;
	running_thread = first_thread;
}

Tid
thread_id()
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	Tid ret = running_thread->tid;
	interrupts_set(enabled);
	return ret;
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	interrupts_on();
	thread_main(arg); // call thread_main() function with arg
	thread_exit(0);
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	bool enabled;

	enabled = interrupts_off();
	assert(!interrupts_enabled());
	if (num_threads == THREAD_MAX_THREADS) {
		interrupts_set(enabled);
		assert(interrupts_enabled());
		return THREAD_NOMORE;
	}

	// find a free tid
	int have_tid;
	int i;
	for (i = 1; i < THREAD_MAX_THREADS; i++) {
		if (available_tids[i] == 0) {
			// found a free tid
			have_tid = i;
			available_tids[i] = 1;
			break;
		}
	}
	// check if we have found a free tid
	if (i == THREAD_MAX_THREADS) {
		interrupts_set(enabled);
		assert(interrupts_enabled());
		return THREAD_NOMORE;
	}
	// create a new thread
	struct thread *new_thread = malloc369(sizeof(struct thread));
	if (!new_thread) {
		available_tids[i] = 0;

		interrupts_set(enabled);
		assert(interrupts_enabled());
		return THREAD_NOMEMORY;
	}
	// // Set interrupts back (enabled) before getcontext
	// interrupts_set(enabled);
	// assert(interrupts_enabled());
	
	int err = getcontext(&new_thread->thread_context); // initialize the context
	assert(err == 0);
	// // disable interrupts after getcontext
	// enabled = interrupts_off();
	// assert(!interrupts_enabled());
	// initialize the new thread
	// put the stack pointer, the program counter, and two argument registers in ucontext_t
	new_thread->thread_stack_pointer = malloc369(THREAD_MIN_STACK); // allocate a stack
	if (!new_thread->thread_stack_pointer) {
		free369(new_thread);
		available_tids[have_tid] = 0;

		interrupts_set(enabled);
		assert(interrupts_enabled());
		return THREAD_NOMEMORY;
	}

	num_threads++;
	ready_queue_enqueue(new_thread);
	new_thread->tid = have_tid;
	new_thread->wq = wait_queue_create();
	assert(new_thread->wq);
	new_thread->in_wq = NULL;
	new_thread->thread_context.uc_mcontext.gregs[REG_RSP] = (unsigned long)new_thread->thread_stack_pointer + THREAD_MIN_STACK + sizeof(unsigned long); // set the stack pointer 
	new_thread->thread_context.uc_mcontext.gregs[REG_RIP] = (unsigned long)thread_stub; // set the function address to thread_stub (program counter)
	new_thread->thread_context.uc_mcontext.gregs[REG_RDI] = (unsigned long)fn; 
	new_thread->thread_context.uc_mcontext.gregs[REG_RSI] = (unsigned long)parg;

	interrupts_set(enabled);
	assert(interrupts_enabled());
	return new_thread->tid;
}

Tid
thread_yield(Tid want_tid)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	if (want_tid == THREAD_SELF || want_tid == running_thread->tid) {
		// keep running the current thread.
		// switch thread with getcontext and setcontext.
		int to_tid = running_thread->tid;
		interrupts_set(enabled);
		return to_tid;
	}
	if (want_tid == THREAD_ANY) {
		// Obtain the next thread from the ready queue.
		if (!rqueue_head->thread || rq_size == 0) {
			interrupts_set(enabled);
			return THREAD_NONE;
		}
		// Look if there exists a non-zombie thread and free zombies

		struct thread *current_thread = running_thread;
		struct thread *temp = running_thread;
		int i = rq_size;
		while (i > 0) {
			int dequeued = ready_queue_dequeue(&temp);
			if (dequeued == 0) {
				// thread not found.
				interrupts_set(enabled);
				return THREAD_INVALID;
			}
			assert(dequeued == 1);
			// yield to a thread that is not in wait queue
			if (temp->in_wq == NULL) {
				running_thread = temp;
				ready_queue_enqueue(current_thread);
				// swap thread's context. Running_thread corresponds to next thread.
				int setcontext_called = 0;
				getcontext(&current_thread->thread_context);
				if (!setcontext_called) {
					setcontext_called = 1;
					current_thread->yield_to_tid = running_thread->tid;
					setcontext(&running_thread->thread_context);
				}
				interrupts_set(enabled);
				return current_thread->yield_to_tid;
			}
			i--;
			ready_queue_enqueue(temp);
		}
		
		interrupts_set(enabled);
		return THREAD_NONE;
	}

	// Specific want_tid
	if (want_tid > THREAD_MAX_THREADS || want_tid < 0 || available_tids[want_tid] == 0) {
		// invalid thread id. or has not created yet
		interrupts_set(enabled);
		return THREAD_INVALID;
	}

	// Find the thread with the given tid.
	struct thread *current_thread = running_thread;
	int curr_rq_size = rq_size;
	int removed = remove_from_ready_queue(want_tid, &running_thread);
	if (removed == 0) {
		// thread not found.
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	assert(removed == 1);
	if (running_thread->in_wq != NULL) {
		// thread is in wait queue
		ready_queue_enqueue(running_thread);
		running_thread = current_thread;
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	assert(curr_rq_size == rq_size + 1);
	assert(running_thread->tid == want_tid);
	int enqueued = ready_queue_enqueue(current_thread);
	assert(enqueued == 1);

	// swap thread's context. Running_thread corresponds to next thread.
	int setcontext_called = 0;
	getcontext(&current_thread->thread_context);
	if (!setcontext_called) {
		setcontext_called = 1;
		current_thread->yield_to_tid = running_thread->tid;
		setcontext(&running_thread->thread_context);
	}

	interrupts_set(enabled);
	return current_thread->yield_to_tid;
}

void
thread_exit(int exit_code)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	// destroy_zombie_queue();
	assert(running_thread->in_wq == NULL);
	int removed = remove_from_ready_queue(running_thread->tid, &running_thread);
	assert(removed == 0);
	struct thread *exiting_thread = running_thread;
	// set the thread state to exited
	exiting_thread->thread_state = EXITED;
	exiting_thread->exit_status = exit_code;
	if (rq_size == 0) {
		num_threads--;
		available_tids[exiting_thread->tid] = 0;
		destroy_zombie_queue();
		free369(exiting_thread);
		interrupts_set(enabled);
		exit(exit_code);
	}
	// Set the exit_code of all the threads in its wait queue
	// and wakeup all the threads in its wait queue.
	int i = 0;
	q_node_t *curr = exiting_thread->wq->wq_head;
	while (i < exiting_thread->wq->wq_size) {
		curr->thread->exit_code_from_parent = exit_code;
		curr = &exiting_thread->wq->waiting_queue[(curr - exiting_thread->wq->waiting_queue + 1) % THREAD_MAX_THREADS];
		i++;
	}

	thread_wakeup(exiting_thread->wq, 1);
	assert(exiting_thread->wq->wq_size == 0);
	// add to zombie queue
	zombie_queue_enqueue(exiting_thread);

	i = rq_size;
	while (i > 0) {
		int dequeued = ready_queue_dequeue(&running_thread);
		assert(dequeued == 1);
		if (running_thread->in_wq == NULL) {
			// switch to the next thread. Running thread corresponds to next thread.
			int setcontext_called = 0;
			getcontext(&exiting_thread->thread_context);
			if (!setcontext_called) {
				setcontext_called = 1;
				setcontext(&running_thread->thread_context);
			}
			destroy_zombie_queue();
			interrupts_set(enabled);
			return;
		}
		i--;
		ready_queue_enqueue(running_thread);
	}
}

Tid
thread_kill(Tid tid)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	if (running_thread->tid == tid) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	if (tid > THREAD_MAX_THREADS || tid < 0 || available_tids[tid] == 0) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	
	struct thread *thread_to_kill = running_thread;
	int removed = remove_from_ready_queue(tid, &thread_to_kill);
	if (removed == 0) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}

	if (thread_to_kill->in_wq != NULL) {
		// thread_to_kill is in other thread's wait queue.
		wait_queue_remove(thread_to_kill->in_wq, tid);
	} 
	// wakeup all the threads that are waiting for this thread.
	// And set the exit_code to -SIGKILL.
	thread_to_kill->exit_status = -SIGKILL;
	int i = 0;
	q_node_t *curr = thread_to_kill->wq->wq_head;
	while (i < thread_to_kill->wq->wq_size) {
		curr->thread->exit_code_from_parent = -SIGKILL;
		curr = &thread_to_kill->wq->waiting_queue[(curr - thread_to_kill->wq->waiting_queue + 1) % THREAD_MAX_THREADS];
		i++;
	}
	thread_wakeup(thread_to_kill->wq, 1);
	zombie_queue_enqueue(thread_to_kill);
	interrupts_set(enabled);
	return tid;
}

/**************************************************************************
 * Important: The rest of the code should be implemented in Assignment 2. *
 **************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	struct wait_queue *wq;

	wq = malloc369(sizeof(struct wait_queue));
	assert(wq);

	wq->wq_size = 0;
	wq->wq_head = wq->wq_tail = wq->waiting_queue;

	interrupts_set(enabled);
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());

	if (wq == NULL || wq->wq_size > 0) {
		interrupts_set(enabled);
		return;
	}
	free369(wq);
	interrupts_set(enabled);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	if (queue == NULL) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	if (rq_size == 0) {
		interrupts_set(enabled);
		return THREAD_NONE;
	}
	struct thread *current_thread = running_thread;
	struct thread *temp = running_thread;
	int i = rq_size;
	while (i > 0) {
		int dequeued = ready_queue_dequeue(&temp);
		assert(dequeued == 1);
		if (temp->in_wq == NULL) {
			running_thread = temp;
			wait_queue_enqueue(queue, current_thread);
			current_thread->in_wq = queue; // set the thread is waiting in this queue.
			ready_queue_enqueue(current_thread);
			int setcontext_called = 0;
			getcontext(&current_thread->thread_context);
			if (!setcontext_called) {
				setcontext_called = 1;
				current_thread->wait_to_tid = running_thread->tid;
				setcontext(&running_thread->thread_context);
			}
			interrupts_set(enabled);
			return current_thread->wait_to_tid;
		}
		i--;
		ready_queue_enqueue(temp);
	}

	interrupts_set(enabled);
	return THREAD_NONE;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	int waked_up = 0;
	destroy_zombie_queue();
	if (queue != NULL && queue->wq_size > 0) {
		if (all) {
			int prev_rq_size = rq_size;
			while (queue->wq_size > 0) {
				struct thread *temp;
				temp = wait_queue_dequeue(queue);
				assert(temp != NULL);
				temp->in_wq = NULL;
				temp->thread_state = READY;
				waked_up++;
			}
			assert(prev_rq_size == rq_size);
			assert(queue->wq_size == 0);
		} else {
			struct thread *temp;
			temp = wait_queue_dequeue(queue);
			wait_queue_remove(queue, temp->tid);
			temp->in_wq = NULL;
			temp->thread_state = READY;
			waked_up++;
		}
		interrupts_set(enabled);
		return waked_up;
	}
	interrupts_set(enabled);
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	struct thread *current_thread = running_thread;
	struct thread *target_thread = running_thread;
	if (!thread_ret_ok(tid) || tid > THREAD_MAX_THREADS || 
				available_tids[tid] == 0 || tid == running_thread->tid) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	// Look for the tid in the ready queue.
	int removed = remove_from_ready_queue(tid, &target_thread);
	if (removed == 0) {
		// target_thread might in zombie_queue
		q_node_t *curr = zombie_queue_head;
		while (curr != zombie_queue_tail) {
			if (curr->thread->tid == tid && curr->thread->exit_status != -SIGKILL) {
				if (exit_code != NULL) {
					*exit_code = curr->thread->exit_status;
				}
				current_thread->exit_code_from_parent = -999;
				destroy_zombie_queue();
				interrupts_set(enabled);
				return tid;
			}
			curr = &zombie_queue[curr - zombie_queue + 1 % THREAD_MAX_THREADS];
		}
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	assert(removed == 1);
	// Add the thread back to the ready queue.
	int enqueued = ready_queue_enqueue(target_thread);
	assert(enqueued == 1);
	if (target_thread->wq->wq_size > 0) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	// Add current thread to the wait queue.
	Tid target_tid = target_thread->tid;
	assert(target_thread->tid == tid);
	Tid ret = thread_sleep(target_thread->wq);
	assert(thread_ret_ok(ret));

	destroy_zombie_queue();

	if (current_thread->exit_code_from_parent != -999) {
		if (exit_code != NULL) {
			*exit_code = current_thread->exit_code_from_parent;
		}
		current_thread->exit_code_from_parent = -999;
		interrupts_set(enabled);
		return target_tid;
	}
	interrupts_set(enabled);
	return THREAD_INVALID;
}

struct lock {
	struct thread *acquired_thread;
	struct wait_queue *lock_wq;
};

struct lock *
lock_create()
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());

	struct lock *lock;

	lock = malloc369(sizeof(struct lock));
	assert(lock);

	lock->lock_wq = wait_queue_create();
	lock->acquired_thread = NULL;

	interrupts_set(enabled);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());

	assert(lock != NULL);
	if (lock->lock_wq->wq_size > 0 || lock->acquired_thread != NULL) {
		return;
	}
	wait_queue_destroy(lock->lock_wq);
	free369(lock);
	interrupts_set(enabled);
}

void
lock_acquire(struct lock *lock)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());

	assert(lock != NULL);
	while (__sync_val_compare_and_swap(&lock->acquired_thread, NULL, running_thread) != NULL) {
		thread_sleep(lock->lock_wq);
	}
	interrupts_set(enabled);
}

void
lock_release(struct lock *lock)
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());
	assert(lock != NULL);

	lock->acquired_thread = NULL;
	thread_wakeup(lock->lock_wq, 1);
	interrupts_set(enabled);
}

struct cv {
	struct wait_queue *cv_wq;
};

struct cv *
cv_create()
{
	bool enabled;
	enabled = interrupts_off();
	assert(!interrupts_enabled());

	struct cv *cv;

	cv = malloc369(sizeof(struct cv));
	assert(cv);

	cv->cv_wq = wait_queue_create();
	assert(cv->cv_wq);
	
	interrupts_set(enabled);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	bool enabled = interrupts_off();
	assert(!interrupts_enabled());
	assert(cv != NULL);

	if (cv->cv_wq->wq_size > 0) {
		interrupts_set(enabled);
		return;
	}
	wait_queue_destroy(cv->cv_wq);
	free369(cv);
	interrupts_set(enabled);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	bool enabled = interrupts_off();
	assert(!interrupts_enabled());

	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->acquired_thread == running_thread) {
		lock_release(lock);
		thread_sleep(cv->cv_wq);
		lock_acquire(lock);
	}
	interrupts_set(enabled);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	bool enabled = interrupts_off();
	assert(!interrupts_enabled());

	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->acquired_thread == running_thread) {
		thread_wakeup(cv->cv_wq, 0);
	}
	interrupts_set(enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	bool enabled = interrupts_off();
	assert(!interrupts_enabled());

	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->acquired_thread == running_thread) {
		thread_wakeup(cv->cv_wq, 1);
		assert(cv->cv_wq->wq_size == 0);
	}
	interrupts_set(enabled);
}
