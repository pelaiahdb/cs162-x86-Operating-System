Final Report for Project 1: Threads
===================================

## Group Members

* Pelaiah Blue <pelaiahdavyblue@berkeley.edu>
* Feynman Liang <feynman@berkeley.edu>
* Kevin Lu <klu95@berkeley.edu>
* Muliang Shou <muliang.shou@berkeley.edu>

# Task 1

For the most part, our implementation of the Efficient Alarm Clock followed our original design doc, though with some minor improvements.

As stated in our design doc, we used a sorted linked list `s_threads` as the data structure to store sleeping threads. In our initial brainstorm, we briefly floated the idea of using a binary heap instead. However, we ultimately decided it would be best to implement `s_threads` as a sorted linked list because it would support `delete_min` in O(1) time and we wanted the code in the interrupt handler to run as fast as possible. In our design review, our TA had confirmed that this was the correct approach. In the actual coding of the task, it very straightforward to implement the linked list as a well-documented library was already provided for us in `lib/kernel/list.h`.

One thing that our TA had questioned in our design doc was our use of a lock on `s_threads`. We had initially forced `timer_sleep` to acquire a lock before modifying the `s_threads` list, i.e. before it is allowed to insert into the list. This was to prevent `timer_interrupt` from trying to traverse/delete from the list at the same time `timer_sleep` is trying to add to the list. After further discussion with our TA, we realized that we did not actually need to use a lock; we could simply disable interrupts before `timer_sleep` attempts to insert into the list. This greatly simplified our implementation while still allowing us to achieve concurrency.

# Task 2

## Scheduling the highest priority thread to run next

Our implementation of priority scheduling closely followed what was described in the design doc. We maintain an unsorted list of threads ready to run in `ready_list` and use `thread_priority_less` as a comparator to `list_max` in order to schedule the next thread to run in `next_thread_to_run`. Within `thread_set_priority`, we added a call to `thread_yield_if_not_highest_pri` to yield to a higher priority thread in case the new priority makes the current thread lower priority than a waiting thread.

## Priority scheduling in synchronization primitives

An identical strategy was used to choose the next thread to unblock from the `waiters` of a `semaphore` except using `sema_elem_less` to compare effective priority of `semaphore_elem`s in the `waiters` list. As lock and condition variables were all built from the `semaphore` primitive, little modification was required to make the other synchronization primitives respect priority scheduling.

Since priority donation was only required for `lock`s, we implemented it within `lock_acquire` and the computation in `thread_effective_priority` to get a thread’s effective priority. During design review, our TA provided an important consideration that we had missed regarding priority donation. Specifically, our original design only maintained a `donated_to` member within `struct thread` which was recursively traversed to handle nested priority donations. However, this did not correctly handle the case of a thread receiving donations from multiple sources (e.g. if a single thread held multiple locks) since we could not determine all the possible donation sources. Instead, our final design added a `locks_held` list to `struct thread` which kept track of all the locks held by the current thread and recursively traversed the `waiters` of these locks to determine the thread’s effective priority.




# Task 3

To begin with, we made some significant departures from our original design for the MLFQS (Multi-Level Feedback Queue Scheduler) by scrapping the previously envisioned implementation of an array of 64 Pintos lists representing the priority queues. Instead of the array, we decided to use a single, pre-existing `ready_list` to simulate multiple queues.
 
During the design review, we discussed this single list implementation with our TA as an alternative to the array scheduler. We had initially considered the idea of a single list, but dismissed it in our first draft because we thought that we needed to sort the list to get the next highest priority thread. The sorting would’ve had a running time of O(nlogn) each time a thread yields or is created, compared to getting the next thread in O(1) with an array. Upon later consideration, we realized that with a single list, we could simply call `list_max` to return the first highest priority thread it finds in the list for O(n). To follow the round-robin rule for the highest priority queue, we simply push the yielding thread at the back of the list.
 
For Task 3, we spent most of our time attempting to make the 64-list solution work. We encountered several problems with the array scheduler, the most inconvenient one being the transferring of threads to the proper queues when their priorities change. Instead of popping only the modified elements, we emptied the array and reinserted the ready threads from `all_elem` to their respective queues. This mass transition runs in O(n) time.
 
To calculate `load_avg`, we needed to calculate the number of ready and running threads. Instead of summing up the length of each of the 64 queues, we traversed `all_elem` to count this number. At this point, we were already doubting the efficiency of our 64-list solution over the single list one. For the single list, we need only get the size of `ready_list` (although this, too, scans the entire list) plus the running thread. While we were stuck on solving a bug in the mass transition, we tried implementing the single list solution on a separate branch.
 
The benefits of using the existing `ready_list` to simulate multiple queues became apparent as we were writing considerably fewer lines of code. We no longer needed to modify most of the existing functions that operate on `ready_list`, such as `thread_unblock()` and `thread_yield()`. Some of the code from the priority scheduler apply to the advanced scheduler as well, minimizing checks for the `thread_mlfqs` flag and making the entire project code much simpler.



# Reflection

## Responsibilities

- Task 1: Efficient Alarm Clock
-- Owned by Kevin
- Task 2: Priority Scheduler
-- Owned by Feynman
-- Includes design doc additional questions (section 2.1.2) Q1
- Task 3: MLFQS
-- Owned by Paolo and Muliang
-- Includes design doc additional questions (section 2.1.2) Q2, Q3

## What went well?
For task #3, our departure from our original model (of 64 queues) is remarkable yet fruitful. Being able to piggyback on the skeleton code is immensely beneficial to our debugging efforts as well as the clarity of the whole project.

## What could have been done better?

Reusing code across different tasks; we had partitioned the work across the three tasks across our team and developed independently, resulting in some duplication of functionality that was reconciled close to the deadline (e.g. `thread_yield_if_not_highest_pri`).
