Design Document for Project 1: Threads
======================================

## Group Members

* Brandon Pickering bpickering@berkeley.edu
* Victor Ngo vngo408@berkeley.edu
* Randy Shi beatboxx@berkeley.edu 
* Alan Tong atong@berkeley.edu

## Task 1: Efficient Alarm Clock

### Data structures and functions

Modificiations:

thread.c:

- `static int64_t ticks`
  Number of timer ticks since OS booted.

- `static struct list waiting_list`
  Keeps a sorted linked list of waiting tasks and when they're scheduled to wake up
 
- `void thread_tick (void)`  
  Checks from the head of the linked list, unblocking threads until reaching one that can't be unblocked yet

- `void thread_block (void)`
  Blocks a thread, requires interrupts off

- `void thread_unblock (struct thread *t)`
  Unblocks a thread

timer.c:

- `void timer_sleep (int64_t ticks) `
  Instead of the while, yield, we instead disable interrupts, block the thread and add the thread to the linked list

### Algorithms

When a thread sleeps, we block it and add it onto the waiting list.
We insert the items into the list sorted to simplify logic later and easily get the min elements
We then unblock any possible threads at thread tick and don't need to go through everything because the list is sorted

### Synchronization

We disable interrupts when a thread sleeps to avoid synchronization problems. With the way lists are implemented in pintos, we don't have to worry about the threads being on different lists, ready and wait. Code in thread_tick is thread_safe because it is called from the timer interrupt handler, which disables interrupts.

### Rationale

We chose to have a list of waiting tasks to avoid busy waiting. This way, we avoid the thread continually yielding and do the little work required to unblock ready threads at each thread time. Insertion takes linear time but each tick takes constant time.


## Task 2: Priority Scheduler

### Data structures and functions

Modifications:

thread.h:

- Add `struct list donators` and `struct list_elem donator_elem` to `struct thread`
  
  Keep track of all threads whose priorities are contributing to this thread via priority donation.
  
  Note that we only need one `list_elem` because a thread can only wait on a single lock at a time.

- Add `struct lock *waiting_lock` to `struct thread`

  If this thread is waiting on a lock, we will need to access its donee.

- Add `int base_priority` to `struct thread`

  Need to restore priority to original priority when releasing the lock.

thread.c:

- `tid_t thread_create (const char *name, int priority, thread_func *function, void *aux)`

  Will need to initialize `priority_list`.

- `static struct thread * next_thread_to_run (void)`

  Should pick a thread with maximum priority.

- `void thread_set_priority (int new_priority)`

  Should yield the thread if the priority is lower than another ready thread's.

- Add `void update_effective_priority (struct thread *thread, int new_priority)`

  Helper function for priority donation. Sets thread's priority, and donates this priority to threads it is waiting on.

synch.c:

- `void sema_up (struct semaphore *sema)`

  Should pick a thread with maximum priority to unblock.

- `void cond_signal (struct condition *cond, struct lock *lock UNUSED)`

  Should pick a waiter whose thread has maximum priority.

- `bool lock_acquire (struct lock *lock)`

  On failure will donate priority from the caller to the owner if necessary.

- `void lock_release (struct lock *lock)`

  Restore owner's priority.

### Algorithms

When we need to pick a thread with maximum priority, we can simply scan linearly through the list.

In `set_thread_priority` and `lock_release`, we should yield so that the scheduler can reschedule the thread (in case the thread now has lower priority than another thread).

#### Priority donation algorithm:

Idea:

1. A thread's priority is always equal to the maximum of its base priority and the priorities of the threads in its donator list.

2. A thread's donator list consists of all threads waiting on any lock owned by the thread.

In order to ensure (1), we need to recalculate priority whenever a lock is acquired or released by the thread, whenever one of its owned locks is waited on, and whenever one of its donators' priority changes.

Modifications:

- `update_effective_priority` should set the thread's priority, and check if it is waiting on a lock (via its `waiting_lock` field). If so, and this lock has owner `T`, check if the new priority is higher than `T`'s priority. If so, recursively call `update_effective_priority` on `T` with the new priority.

- In `thread_create`, initialize `donator_list` to be empty and `base_priority` to be the thread's initial priority.

- When `set_thread_priority` is called, set the thread's base priority to the new priority. Then call `update_effective_priority` to set its priority to the maximum of its current priority and its new base priority.

- When a lock is successfully acquired, add all the lock's waiters to the owner's donator list. Set the owner's priority to be the maximum of its base priority and the priorities of all its donators. (Equivalently, the maximum of the owner's current priority and the priorities of all this lock's waiters).

  We can use `update_effective_priority` here, but it is unnecessary since the owner could not possibly be waiting on another lock because it is running currently.

- When `lock_acquire` is called on an owned lock, add the caller thread to the owner's donator list. Set the owner's priority (`update_effective_priority`) to be the maximum of its current priority and the caller's priority.

- When `lock_release` is called, remove all the lock's waiters from the owner's donator list. This is okay since the donator list cannot have duplicates (a thread can only wait on one lock at a time). Then set the owner's priority (`update_effective_priority`) to be the maximum of its base priority and the priorities of all its remaining donators.

### Synchronization

Most of the modifications are in places protected by disabled interrupts or semaphores, so they will not cause race conditions.

### Rationale

Picking a thread to run with maximum priority takes linear time. This was selected for simplicity of coding and to prevent changing the data structure containing ready threads. A priority queue or some other structure may be faster, but this can be changed easily in the future, and may depend on the details of the scheduling algorithm.

When changing priority, we considered searching the other ready threads in order to determine if the current thread should yield. However, this would require disabling interrupts to prevent race conditions with `ready_list`, and the scan could be large. It is simpler and probably faster in most cases to yield no matter what. Furthermore, such an explicit check would make assumptions about the scheduling algorithm, which could change in the future.

The priority donation algorithm forces us to iterate through `donator_list` for several operations. This will be okay under the assumption that a single thread will usually own a small number of locks at once. If we allowed a thread to give up its priority donation prematurely in some cases, then the algorithm could be greatly simplified and sped up, but the current algorithm is designed to be correct in all cases.



## Task 3: MLFQS

### Data structures and functions

Modificiations:

thread.h:

- `struct thread`
  - add `fixed_point_t recent_cpu`
  - add `int nice`
  - add `int priority`

thread.c:

- add `fixed_point_t load_avg`

- add `static struct list[64] priority_queues`

- add `int mlfqs_tick`
 
- `void thread_tick (void)`
  - if `mlfqs_tick` reaches `4*TIMER_FREQ`, set it to `0`
  - decrement `recent_cpu` of running thread
  - if `mlfqs_tick` reaches multiple of `4`, recalculate the priority of current thread, move current thread to the end of its priority's queue (for round robin)
  - if `mlfqs_tick` reaches multiple of `TIMER_FREQ`, recalculate `load_avg`, iterate through each thread and update `recent_cpu` and priority, removing them from their old priority's queue if it was on one and pushing them into their new priority's queue if the thread is running.

- `void thread_set_nice (int)`
  
  changes current thread's nice, updates priority and yield to make sure the highest priority thread runs

- `int thread_get_nice (void)`
  
  return the nice of current thread

- `int thread_get_load_avg(void)`
  
  calculate `load_avg` using length of `ready_list`, rounded to nearest integer

- `int thread_get_recent_cpu(void)`
  
  multiply current thread's `recent_cpu` by 100, return that rounded to nearest integer

- `static struct thread *next_thread_to_run (void)`
  iterate through the priority_queues from highest to lowest priority to find highest priority queue that has a running thread, pop a thread from that queue and return that thread.

- `void thread_block (void)`
  makes sure to remove the thread from its priority's queue

- `void thread_unblock (struct thread *t)`
  makes sure to add the thread to its priority's queue


### Algorithms

To calculate priority:
  first calculate load_avg = (59/60) × load_avg + (1/60) × ready_threads, in that order of operations
  then use that to calculate the for each thread recent_cpu = (2 × load_avg)/(2 × load_avg + 1) × recent_cpu + nice, in that order of operations
  then update priority to PRI_MAX − (recent_cpu/4) − (nice × 2), in that order of operations

For every 4th tick of `void thread_tick (void)`, it's only necessary to recalculate priority of current thread because load_avg and recent_cpu do not change until every TIMER_FREQ(100th) tick in which case all priorities are recalculated, or they could change for a thread whose nice value was changed, but that function `void thread_set_nice (int)` already makes sure that the CPU will yield to highest priority thread.

To get next_thread_to_run in `static struct thread *next_thread_to_run (void)`, simply iterate through `static struct list[64] priority_queues` from highest to lowest priority and pop and return a thread from the highest priority non-empty queue.
  

### Synchronization

Most of the modifications are in places protected by disabled interrupts, so they will not cause race conditions.

For `thread_set_nice`, we are modifying the thread queues. This could cause issues if a thread switch occurs. Because the scheduler itself uses these queues, it is best to disable interrupts completely for this operation.

### Rationale

Since there are a constant number of priorities, creating an array of 64 queues and iterating through them from highest to lowest priority is an efficient way to find the highest priority ready/running thread regardless of the number of threads. It is easier to implement than having a list of threads sorted by priority, and performs better if there are many threads since there are a constant amount of priorities. It also doesn't take up too much memory, since 64 linked lists uses roughly the same amount of memory as 1 linked list with the same number of elements.

Recalculating priority of only current thread rather than every thread every 4th tick is more efficient and should produce the same results, as explained in the Algorithms section.



## Additional Questions

1. Expected: `GOOD`. Actual: `BAD`.

  Start with a lock `L`, a semaphore `S = 0`, and a thread `A` with priority `1`.

  Code for thread A:

  ```
  spawn thread D with priority 0
  acquire L
  spawn thread C with priority 3 (C will donate its priority of 3 to A)
  spawn thread B with priority 2
  call down on S
  print GOOD
  ```

  Code for thread B:

  ```
  call down on S
  print BAD
  ```

  Code for thread C:

  ```
  try to acquire L
  ```

  Code for thread D:
  ```
  call up on S (A and B are waiting. A has eff. 3 and base 1, while B has both 2)
  ```

2. MLFQS table:

  timer ticks | R(A) | R(B) | R(C) | P(A) | P(B) | P(C) | thread to run
  ------------|------|------|------|------|------|------|--------------
  0           |     0|     0|     0|    63|    61|    59| A
  4           |     4|     0|     0|    62|    61|    59| A
  8           |     8|     0|     0|    61|    61|    59| A
  12          |    12|     0|     0|    60|    61|    59| B
  16          |    12|     4|     0|    60|    60|    59| A
  20          |    16|     4|     0|    59|    60|    59| B
  24          |    16|     8|     0|    59|    59|    59| A
  28          |    20|     8|     0|    58|    59|    59| B 
  32          |    20|    12|     0|    58|    58|    59| C
  36          |    20|    12|     4|    58|    58|    58| A 

3. The question didn’t specify the `PRI_MAX` and the number of ticks per second so we assumed their values based on other parts of the project spec. We assumed `PRI_MAX = 63`, and that there are 100 ticks per second (meaning we do not consider any formulas containing `load_avg` since there are only 36 ticks). We also assumed that threads with the same priority are ordered in a round-robin fashion when picking which one to run. In this case we assume "round-robin" is alphabetical order.

