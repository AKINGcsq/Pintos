Final Report for Project 1: Threads
===================================

## Task 1: Efficient Alarm Clock

Changes:

- After discussion with the TA, we made our algorithm less complex and easier to implement.

- Moved most of the code to the interrupt handler instead of thread_tick, even though the handler calls thread_tick, to make the timer code more modular. Found out that for disabling interrupts we had to save the old interrupt state and restore it after.


## Task 2: Priority Scheduler

Changes:

- After discussion with the TA, had to modify the basic algorithm to handle the case where a donator's priority changes.

- Had to be stricter about synchronization than expected, because, e.g. we need to make sure that no thread tries to acquire a lock immediately after another thread successfully acquires it (before inheriting the lock's waiters as donators).

- Some of the "shortcuts" for updating priority that I put in the design doc are actually incorrect, and had to be replaced with the full version (mainly because I forgot to account for cases where priority has to be decreased rather than increased).


## Task 3: MLFQS

Changes:

- After discussion with TA, used one unsorted list of priorities instead of 64 different queues


## Reflection

Roles:

- Victor: Task 1
- Brandon: Task 2
- Alan: Task 3
- Randy: General debugging, refactoring, styling

What went well: The project went pretty smoothly because of careful design. Most of the mental effort for the project went into the design doc so that we didn't have to come up with algorithms while coding.

What could be improved: More careful testing. There were several points where we passed all the tests but knew that our code was incorrect/incomplete.
