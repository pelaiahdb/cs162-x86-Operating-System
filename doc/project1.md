Design Document for Project 1: Threads
======================================

## Group Members

* Pelaiah Blue <pelaiahdavyblue@berkeley.edu>
* Feynman Liang <feynman@berkeley.edu>
* Kevin Lu <klu95@berkeley.edu>
* Muliang Shou <muliang.shou@berkeley.edu>

# Task 1: Efficient Alarm Clock
## Data Structures and Functions

```c
/* (New) A linked list of sleeping threads. This will be kept in sorted order with respect to wake_time */
static struct list s_threads
```

```c
/* relevant functions */
struct list_elem * list_begin (struct list *list)
struct list_elem * list_pop_front (struct list *list)
void list_insert_ordered (struct list *list, struct list_elem *elem, list_less_func *less, void *aux)
```


```c
/* (New) A lock on the list `s_threads`. Prevents making changes to the list until the lock has been acquired. */
static struct lock s_threads_lock;
```

```c
/* relevant functions */
void lock_init (struct lock *lock)
void lock_acquire (struct lock *lock)
void lock_release (struct lock *lock)
bool lock_try_acquire(struct lock *lock)
```

```c
/* (New) Comparator for thread `wake_time` */
bool wake_time_less(struct thread *a, struct thread *b, void *aux) {
   return a->wake_time < b->wake_time;
}
```

```c
/* (Modified) Sleeps for approximately `TICKS` timer ticks. Interrupts must be turned on. */
void timer_sleep (int64_t ticks) {
    thread_current()->wake_time = timer_ticks() + ticks;
    lock_acquire(s_thread_lock);
    list_insert_ordered(s_threads, thread_current(), wake_time_less, NULL);
    lock_release(s_thread_lock);
}
```

```c
/* (Modified) Timer interrupt handler. */
static void timer_interrupt (struct intr_frame *args UNUSED) {
    list_elem _thread * = list_begin(s_threads);
     while (lock_try_acquire(s_threads_lock) && _thread != NULL) {
if (_thread->wake_time && timer_ticks() >= _thread->wake_time) {
        list_pop_front (s_threads);
        thread_unblock(_thread);
         _threadwake_time = NULL;
_thread = list_begin(s_threads);
}
else {
    _thread = _thread->next
}
    }
}
```

```c
/* (Modified) Thread struct */
struct thread {
    …
    int64_t wake_time;  /* The time when the thread should wake up. */
};
```

## Algorithms

`s_threads` uses the linked list data structure provided in `lib/kernel/list.h`. It will be used to keep track of sleeping threads, and will be kept in sorted order with respect to the threads’ `wake_time`. Consequently, insertions into this linked list will take O(n) time.

`s_thread_lock` uses the lock implementation provided in `threads/synch.h`. It must be acquired by `timer_sleep()` before it is allowed to make changes to `s_threads`. `timer_interrupt()` cannot acquire a lock, but it shall check if the lock is being held before it is allowed to traverse or modify `s_threads`.

`timer_sleep()` takes ticks as an argument and sleeps the thread for that many ticks. It will store timer_ticks() + ticks to wake_time, blocks the thread (via lock_acquire), and inserts the thread into the s_threads list in order. list_insert_ordered() is conveniently made available for us to use; because the threads should be ordered by wake_time, we use wake_time_less as our less function.

`timer_interrupt()` will check the first element of the linked list in a loop. So long as the ordering is maintained, this first thread will be (one of) the thread(s) with the soonest `wake_time` and `thread_n+1.wake_time` >= `thread_n.wake_time`. If the thread should wake up (current tick > `thread.wake_time`), it will pop the element off the list, and unblock the thread. This will loop until there are no more elements in the list, or we reach a thread is not supposed to wake up yet.

## Synchronization

We force `timer_sleep()` to acquire a lock before modifying the `s_threads`. `timer_interrupt()` cannot acquire a lock, but it should check if the lock is being held before acting on `s_threads`. Conveniently, all of these functions are available to us, so it should not be difficult to implement.

Since any thread that needs to added to `s_threads` is already blocked beforehand, we don’t have to worry about unexpected behaviour occurring when `lock_acquire()` is waiting for a lock or during the traversal phase of `list_insert_ordered()`. The thread will not run again until it is unblocked by the interrupt handler.

By using a while loop on the first element in `s_threads`, `timer_interrupt()` will be able to unblock multiple threads on the same tick, if all of them should wake up on the current tick.

## Rationale

We initially wanted to implement `s_threads` as a binary min-heap, because as long as heap-order property is preserved, the root of the heap would always be the thread with the soonest `wake_time`. Additionally, a binary heap would outperform a sorted linked list for insertions: O(log n) vs O(n).

However, a sorted linked list would outperform a binary heap for `delete_min`: O(1) vs O(log n). Because the list is kept in sorted order, `delete_min` is a simple matter of popping off the first element of the list. **Since we want the code that runs in interrupt handlers, i.e. `timer_interrupt`, should be as fast as possible, we decided it would be best to implement `s_threads` as a sorted linked list.**

Furthermore, a linked list data structure is already provided for us in `lib/kernel/list.h` with most, if not all, relevant functions already implemented. This makes using the linked list a no-brainer; it saves us the effort of having to code up a new data structure ourselves.

**Min Heap:**
 * insert: O(log n)
 * min: O(1)
 * delete_min: O(log n)


**Unsorted Linked List:**
 * insert: O(n)
 * min: O(n)
 * delete_min: O(n)

**Sorted Linked List:**
 * insert: O(n)
 * min: O(1)
 * delete_min: O(1)



# Task 2: Priority Scheduler

## Data Structures and Functions
We add a function for computing effective priority

```c
int
thread_effective_priority (struct thread *t)
{
  return t->priority + t->donated_priority;
}
```

We introduce a comparator for ordering `struct thread`s by their effective priority

```c
bool
thread_priority_less (const struct list_elem *a_, const struct list_elem *b_,
                      void *aux UNUSED)
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  return thread_effective_priority(a) < thread_effective_priority(b);
}
```

We introduce a function which checks if the current thread has highest effective priority, yielding if this is not the case:

```c
void
thread_yield_if_not_highest_priority ()
{
  struct thread *t = list_entry(list_max(&ready_list, thread_priority_less, NULL), struct thread, elem);
  if (thread_get_priority() < t->priority)
    thread_yield();
}
```

We will introduce new fields to `struct thread`:

```c
struct thread
  {
    ...
    int donated_priority;                  /* Priority temporarily donated from higher priority blocked threads. */
    struct thread *donated_to;             /* Thread with a priority donation from current thread (used for recursive donations).  */
    struct thread *received_donation_from; /* Thread which received a priority donation from current thread. */
    ...
  };
```

As well as functions for donating and clearing donated priority:

```c
void
thread_donate_priority (struct thread *other)
{
  thread_current()->donated_to = other;
  if (thread_effective_priority(other) < thread_get_priority()) {
    other->donated_priority += thread_get_priority() - thread_effective_priority(other);
    other->received_donation_from = thread_current();
    if (other->donated_to) {
      thread_donate_priority(other->donated_to);
    }
  }
}
```

```c
void
thread_clear_donated_priority (void)
{
  thread_current()->donated_priority = 0;
  thread_current()->received_donation_from->donated_to = NULL;
  thread_current()->received_donation_from = NULL;
  thread_yield_if_not_highest_priority();
}
```

## Algorithms

### Choosing the next thread to run
We will use an unsorted linked list (provided by `<list.h>`) to represent `ready_list`. To implement priority scheduling, `next_thread_to_run()` will be modified to schedule the highest effective priority thread in `ready_list`.

This will be accomplished using the provided `list_max()` function with `thread_effective_priority()` used as the comparator. This has the following performance implications:
- Finding the highest effective priority thread (used in `next_thread_to_run()` and `thread_yield_if_not_highest_priority()`): O(N)
- Adding a thread to `ready_list`: O(1)

Since we compute the effective priorities inside the call of `next_thread_to_run()`, this solution will handle threads in `ready_list` whose effective priorities change due to priority donation.

### Acquiring a Lock
Priority donation will be implemented within `lock_acquire()`. Before decrementing semaphore backing the lock (i.e. calling `sema_down()`), the current thread will first check if `lock->holder != NULL` and call `thread_donate_priority(lock->holder)` if this holds.

`thread_donate_priority(struct thread *t)` does the following:
1. Sets the current thread’s `donated_to` to `t`
2. If `t` can receive a donation (i.e. has lower effective priority than the current thread):
 a.  Increments `t->donated_priority` such that `t`’s effective priority matches the current thread’s
 b. Recursively performs this update on `t->donated_to` if it exists

Keeping track of `donated_to` and recursively traversing them allows us to handle nested/recursive priority donations.

### Releasing a Lock

When a lock is released, the donated priority of the releasing thread must be reset.
This can be accomplished by calling `thread_clear_donated_priority()` after calling `sema_up`.

In addition, the `donated_to` field in the donating thread must be set to `NULL`.

Note that in this case we do not need to worry about clearing nested / recursive priority donations. This is because
by the time the current thread calls `lock_release()`, any thread which has received a priority donation (i.e. was
blocking the current thread from acquiring the lock) has already called `lock_release()` and hence
`thread_clear_donated_priority()` already.

### Computing the effective priority
See `thread_effective_priority()`.

### Priority scheduling for semaphores and locks
Since `lock`s and `semaphore`s both rely on `sema_up()` when unblocking threads for rescheduling, priority scheduling on locks and semaphores should modify this logic to unblock the highest effective priority thread. This can be accomplished much like the change in `next_thread_to_run()`: use `list_max()` with the effective priority comparator `thread_priority_less()` to choose which of the `sema->waiters` to call `thread_unblock()` on.

### Priority scheduling for condition variables
The monitor lock is implemented using `lock`, so things like priority donation and priority scheduling are delegated to `lock_acquire()`, `lock_release()`, and `sema_up()`.

To ensure that `cond_signal()` respects priority scheduling while waking up waiting threads, the `semaphore` selected from `cond->waiters` to be `sema_up()`ed. Since the `struct semaphore_elem waiter` is a local variable defined in `cond_wait()`, we may assume each `semaphore` in `cond->waiters` has exactly one thread blocked on it. Hence, `cond_signal()` can select the `semaphore` from `cond->waiters` which has the highest effective priority thread associated with the semaphore before calling `sema_up`.

### Changing thread’s priority
To ensure that a thread which no longer has the highest “effective priority” yields to the CPU, we need to check if the current thread has highest priority anytime the current thread’s priority may decrease. There are two possible places where this can occur:
- Changing to a lower priority via `thread_set_priority()`
- Losing donated priority via `thread_clear_donated_priority()`)

To ensure the CPU is yielded, we need to call `thread_yield_if_not_highest_priority()` after either of these events.


## Synchronization
The `semaphore` implementation provided by Pintos implements the suggestions from Lecture 8 and disables interrupts only during `sema_up()` and `sema_down()` rather than through the entire critical section. This will mitigate synchronization issues when the semaphore accesses share data in `sema->waiters` during `sema_down()` and `sema_up()`. If interrupts were not disabled, inserting, and popping from the list may no longer respect effective priority ordering.


## Rationale
We considered using an ordered list for `ready_list`, with ordering maintained using `list_insert_ordered()`. However, this design would require reordering the list upon every priority donation event or `thread_set_priority()` call.

## Additional Question (Section 2.1.2) #1
This test is already covered by `tests/threads/priority-donate-sema.c`:

```c
/*
   Low priority thread L acquires a lock, then blocks downing a
   semaphore.  Medium priority thread M then blocks waiting on
   the same semaphore.  Next, high priority thread H attempts to
   acquire the lock, donating its priority to L.

   Next, the main thread ups the semaphore, waking up L.  L
   releases the lock, which wakes up H.  H "up"s the semaphore,
   waking up M.  H terminates, then M, then L, and finally the
   main thread.

   Written by Godmar Back <gback@cs.vt.edu>. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

struct lock_and_sema
  {
    struct lock lock;
    struct semaphore sema;
  };

static thread_func l_thread_func;
static thread_func m_thread_func;
static thread_func h_thread_func;

void
test_priority_donate_sema (void)
{
  struct lock_and_sema ls;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  lock_init (&ls.lock);
  sema_init (&ls.sema, 0);
  thread_create ("low", PRI_DEFAULT + 1, l_thread_func, &ls);
  thread_create ("med", PRI_DEFAULT + 3, m_thread_func, &ls);
  thread_create ("high", PRI_DEFAULT + 5, h_thread_func, &ls);
  sema_up (&ls.sema);
  msg ("Main thread finished.");
}

static void
l_thread_func (void *ls_)
{
  struct lock_and_sema *ls = ls_;

  lock_acquire (&ls->lock);
  msg ("Thread L acquired lock.");
  sema_down (&ls->sema);
  msg ("Thread L downed semaphore.");
  lock_release (&ls->lock);
  msg ("Thread L finished.");
}

static void
m_thread_func (void *ls_)
{
  struct lock_and_sema *ls = ls_;

  sema_down (&ls->sema);
  msg ("Thread M finished.");
}

static void
h_thread_func (void *ls_)
{
  struct lock_and_sema *ls = ls_;

  lock_acquire (&ls->lock);
  msg ("Thread H acquired lock.");

  sema_up (&ls->sema);
  lock_release (&ls->lock);
  msg ("Thread H finished.");
}
```

This test works as follows:

1. Creates a low priority thread “L” which acquires a lock
2. “L” tries to down a semaphore with value 0 and blocks
3. Creates a medium priority thread “M” which blocks while trying to down the same semaphore
4. Creates a high priority thread “H” which attempts to acquire the lock owned by “L” and blocks
  a. If priority donation is implemented, this event should result in “H” donating it’s priority to “L”
5. The main thread then ups the semaphore and one of the two waiters (“M” and “L”) is unblocked
  a. If priority donation is implemented and the semaphore accounts for priority donations when selecting which thread to unblock, then since “L” has effective priority the same as “H”, it should take precedence over “M” and unblock first.
6. “L” then releases the lock “H” is blocked on, losing its donated priority and waking up “H”
7. Since “H” has higher priority than “L”, it is scheduled to execute next
8. “H” ups the semaphore, waking up “M”
9. All three threads are now done and should terminate in base priority order: “H” followed by “M” followed by “L”

The expected output, assuming priority donation is implemented and accounted for in the implementation of semaphores, is:

```bash
(priority-donate-sema) begin
(priority-donate-sema) Thread L acquired lock.
(priority-donate-sema) Thread L downed semaphore.
(priority-donate-sema) Thread H acquired lock.
(priority-donate-sema) Thread H finished.
(priority-donate-sema) Thread M finished.
(priority-donate-sema) Thread L finished.
(priority-donate-sema) Main thread finished.
(priority-donate-sema) end
```

Assuming the bug described in `sema_up()` exists (i.e. semaphores don’t account for priority donations), the actual output would be:

```bash
(priority-donate-sema) begin
(priority-donate-sema) Thread L acquired lock.
(priority-donate-sema) Thread L downed semaphore.
(priority-donate-sema) Thread M finished.
/* deadlock occurs here*/
```
This is because the semaphore unblocks “M” before “L” since it didn’t account for “L”’s priority donation, resulting in “M” successfully downing the semaphore back to 0 and finishing. “L” remains blocked on the 0 valued semaphore and “H” remains blocked on the lock held by “L”, resulting in deadlock.



# Task 3: Multilevel Feedback Queue Scheduler

## Data Structures and Functions

### New Object Variables

We introduce the new `int thread->nice` variable and `fixed_point_t thread->recent_cpu` variable to `struct thread`.
```
struct thread
  {
    ...
    fixed_point_t recent_cpu;                   /* Thread's recent CPU use */
    int nice;                                   /* Thread's nice value */
    ...
  };
```

### Global Variables

The original `ready_list` is replaced by an array of 64 Pintos lists, each representing a ready queue of threads with the same priority.
```
static struct list ready_queues[64];
```
The highest `thread->priority` value among all ready and running threads is saved in this global variable after updating their priorities. This serves as the index of the highest priority queue.
```
static int highest_priority;
```
This is the number of threads currently ready or running, and is used for computing `load_avg`.
```
static int ready_threads;
```
This system-wide variable estimates the number of ready threads over the past minute, and is updated every second.
```
static fixed_point_t load_avg;
```

### Modified Functions
We modify `thread_init()` to initialize the 64 ready queues instead of `ready_list` if `thread_mlfqs` is turned on.
```
void
thread_init (void) 
{
  ...
  //(new)
  if(thread_mlfqs) {
    int i;
    for (i = 0; i < 64; i++) {
      list_init(&ready_queues[i]);
    }
    ready_threads = 0;
  }
  else {
    list_init (&ready_list);
  }
  ...
}
```
We modify the basic thread initialization to initalize our new thread elements `nice` and `recent_cpu`.
```
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ...
  t->nice = 
  t->recent_cpu = 
  t->magic = THREAD_MAGIC;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}
```
We modify `thread_tick()`, which is called by the timer interrupt handler every tick. In this modification, our advanced scheduler updates the following variables: every tick, `thread->recent_cpu` of running thread increments by 1; every 4 ticks, the `thread->priority` of all ready and running threads are updated and `highest_priority` is set; every second (1 second = 100 ticks by default), the `load_avg` is updated, and the `thread->recent_cpu` of all ready and running threads are updated.
```
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  //(new)
  //  Increment recent_cpu of running thread by 1 every time a timer interrupt occurs
  if (thread_mlfqs) {
    fixed_point_t onef = fix_int(1);
    t->recent_cpu = fix_add(t->recent_cpu, onef);
  }

  if (timer_ticks() % TIMER_FREQ == 0) {
    thread_foreach(update_recent_cpu, void);
  }
  ...
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE) {
    intr_yield_on_return ();
    // recalc all thread priorities
    if (thread_mlfqs) {
      highest_priority = 0;
      thread_foreach(thread_update_priority, void);
    }
  }

  if (timer_ticks() % TIMER_FREQ == 0) {
    update_load_avg();
  }
}
```


We modify the function that transitions a blocked thread to ready. The blocked thread is pushed into the ready queue of its priority if `thread_mlfqs` is turned on.
```
void
thread_unblock (struct thread *t)
{
  if(thread_mlfqs) {
    // insert blocked thread to the ready queue of its priority
    list_push_back (&ready_queues[t->priority], &t->elem);
    ready_threads++;
  }
  else {
    list_push_back (&ready_list, &t->elem);
  }
}
```

We modify the function that yields the CPU. If `thread_mlfqs` is on, the current thread is pushed back to the ready queue of its priority.
```
void
thread_yield (void)
{
  ...
  //(new)
  if (cur != idle_thread) {
    if (thread_mlfqs) {
      list_push_back (&ready_queues[cur->priority], &cur->elem);
      ready_threads++;
    }
    else {
      list_push_back (&ready_list, &cur->elem);
    }
  }
  ...
}
```

We modify the function that chooses and returns the next thread to be scheduled. If `thread_mlfqs` is on, this function returns a thread from the highest priority queue (or the `idle_thread` if empty).
```
static struct thread *
next_thread_to_run (void)
{
  //(new)
  if (thread_mlfqs) {
    if (list_empty (&ready_queues[highest_priority])) {
      return idle_thread;
    }
    else {
      return list_entry (list_pop_front (&ready_queues[highest_priority]), struct thread, elem);
      ready_threads--;
    }
  }
  else {
    if (list_empty (&ready_list))
      return idle_thread;
    else
      return list_entry (list_pop_front (&ready_list), struct thread, elem);
  }
}
```

### New Functions
This sets the current thread's nice value to `nice` and recalculates the thread's `priority` based on the new value. If the running thread no longer has the highest priority, it should yield the CPU.
```
void
thread_set_nice (int nice UNUSED)
{
  thread_current ()->nice = nice;
  thread_update_priority(thread_current());
  thread_set_priority(0);
  if (thread_current()->priority < highest_priority) {
    thread_yield();
  }
}
```

This returns the current thread's nice value.
```
int
thread_get_nice (void)
{
  return thread_current ()->nice;
}
```

This returns the system load average multiplied by 100.
```
int
thread_get_load_avg (void)
{
  return fix_round(fix_scale(load_avg, 100));
}
```
This function updates the load_avg and is called every 1 second.
```
void
update_load_avg() {
  fixed_point_t onef = fix_int(1);
  load_avg = fix_add(fix_unscale(fix_scale(load_avg, 59), 60), fix_unscale(fix_scale(onef, ready_threads), 60));
}
```

This returns 100 times the current thread's recent_cpu value.
```
int
thread_get_recent_cpu (void)
{
  return fix_round(fix_scale(thread_current()->recent_cpu, 100));
}
```

This function updates the priority of a given thread, and is applied to all ready and running threads every 4 ticks.
```
void
thread_update_priority(struct thread *update_thread) {
/* Implements fixed-point real arithmetic of real_priority = PRI_MAX - (recent_cpu/4) - (nice*2). Not using the fixed-point real functions will result in incorrect values */
  fixed_point_t real_priority, real_PRI_MAX, scaled_real_nice;
  real_PRI_MAX = fix_int(PRI_MAX);
  scaled_real_nice = fix_int(update_thread->nice * 2);
  real_priority = fix_sub(fix_sub(real_PRI_MAX, fix_unscale(update_thread->recent_cpu, 4)),scaled_real_nice);
  int new_priority = fix_trunc(real_priority);
  update_thread->priority = new_priority;
  if (new_priority > highest_priority) {
    highest_priority = new_priority;
  }
}
```

We add a new function that updates the given thread's `thread->recent_cpu`. This is applied to all ready and running threads every 1 second.
```
void
update_recent_cpu (struct thread *update_thread) {
  struct fixed_point_t r_cpu = update_thread->recent_cpu;
  struct fixed_point_t coeff1 = (fix_scale(load_avg, 2));
  struct fixed_point_t coeff2 = (fix_add(fix_scale(load_avg, 2), fix_int(1)));
  struct fixed_point_t coeff3 = fix_div(coeff1, coeff2);
  struct fixed_point_t result = fix_add(fix_mul(coeff3, r_cpu), fix_int(update_thread->nice));

  update_thread->recent_cpu = result;
}
```

## Algorithms

### Initialization
If the `thread_mlfqs` switch is turned on, skip initialization of default `ready_list` and instead initialize each of the 64 ready queues as an empty list (`list.h\list`).

### On Thread Creation

Initialize the new thread's elements:
* `thread->nice` = 0 for initial thread, otherwise inherit parent's value
* `thread->recent_cpu` = 0 for initial thread, otherwise inherit parent's value
* use default for all other elements except `priority`

Calculate new thread's priority:
* apply `thread_update_priority()` to new thread
* update global `highest_priority` if the new thread's `priority` is greater
* if new thread's `priority` is greater than `highest_priority`, yield running thread


### Every 1 timer tick

Increment the running thread's `current_thread()->recent_cpu` by 1.

Yield running thread if it has used up all of its `TIME_SLICE` (`thread_ticks()` >= `TIME_SLICE`)

### Every 4 timer ticks

Reset global `highest_priority` to zero

Update priorities of all threads using `thread_foreach(thread_update_priority(), NULL)` (must ignore `idle_thread`)

Get `highest_priority` among all threads

Place each thread to its respective ready queue

### Every 100 timer ticks
(`timer_ticks() % TIMER_FREQ == 0`, where default `TIMER_FREQ` = 100 ticks)

Update global `load_avg` using fixed-point real number functions

Update `thread->recent_cpu` of all threads (makes use of `load_avg`)

### On thread yield

Push current thread at the back of the highest priority queue

Choose next thread to run

### On choosing next thread to run

If highest priority queue is not empty:

* pop the front element off the highest priority queue, "round robin" style

Else (takes care of early exit case), look-up next highest priority nonempty queue:
* if found, set `highest_priority` to that queue's index and pop its front element to return
* else, return `idle_thread`


### On calling `thread_set_nice()` (for test framework)

Set new `current_thread()->nice` value of current thread

Update its `current_thread()->priority`

Update global `highest_priority`

Yield current thread if current_thread()->priority is less than highest_priority


## Synchronization

Interrupts are disabled when accessing kernel thread elements that include `priority`, `recent_cpu` and `nice`. No kernel thread should run before updating `load_avg`, `highest_priority`, and `ready_threads`.

Some of the modified functions require a condition check for the `thread_mlfqs` flag, especially for disabling priority donation. Conflicts with the Task 2 scheduler are resolved by using this check.
```
if (thread_mlfqs) {
    ...
}
else {
    // default scheduler
}
```


## Rationale

We chose to use an ordinary array of Pintos lists to represent the 64 ready queues of the scheduler. We considered other data structures, such as the binary heap and the hashmap, to see if they could be of use to us. Each queue holds threads of the same priority and hence is not a priority queue. It does not need to be sorted, so the ordered property of the binary heap isn’t needed. The highest priority queue runs in “round robin” order, thus a simple linked list works best for the queue (push and pop operations are both O(1)). As for the hashmap, the equations and parameters we use to calculate priority already act as the hash function that maps the threads into one of the 64 bins. With a simple array, looking up the highest priority nonempty queue is solved by keeping a `highest_priority` global value that acts as its index (look-up is reduced to O(1)). We update the highest_priority when computing the priority of each queue.

While we tried to avoid traversing the entire array to look for the highest priority nonempty queue--by using a `highest_priority` global variable (O(1) vs O(63))--we could not avoid it in the case that the highest priority queue (queue with index `highest_priority`) is empty because the current thread (presumably with the current highest priority) exits before the recalculation of the current `highest_priority` every 4 ticks.



## Additional Questions (Section 2.1.2) #2 and #3

timer ticks | R( A ) | R( B ) | R( C ) | P( A ) | P( B ) | P( C  ) | thread to run
------------|------:|------:|------:|------:|------:|------:|:------------:
 0            |   0  |   0  |   0  |  63  |  61  |  59  |  A
 4           |   4  |   0  |   0  |  62  |  61  |  59  |  A
 8           |   8  |   0  |   0  |  61  |  61  |  59  |  B (tie)
12           |   8  |   4  |   0  |  61  |  60  |  59  |  A
16           |  12  |   4  |   0  |  60  |  60  |  59  |  B (tie)
20           |  12  |   8  |   0  |  60  |  59  |  59  |  A
24           |  16  |   8  |   0  |  59  |  59  |  59  |  C (tie)
28           |  16  |   8  |   4  |  59  |  59  |  58  |  B (tie)
32           |  16  |  12  |   4  |  59  |  58  |  58  |  A
36           |  20  |  12  |   4  |  58  |  58  |  58  |  C (tie)


At least two threads were in the highest priority queue on ticks 8, 16, 24, 28, and 36.
The thread with the least recent_cpu is chosen in this particular example. The highest priority queue moves round-robin style.

// the once-per-second `recent_cpu` update formula was not used since 1 second = 100 ticks

