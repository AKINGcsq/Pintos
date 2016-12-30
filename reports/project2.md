Final Report for Project 2: User Programs
=========================================

## Task 1: Argument Passing

Changes:

- Originally we thought it would be sufficient to parse the arguments in `process_execute`, but we actually needed to do it in one of the functions called by the new process (as pointed out by the TA).


## Task 2: Process Control Syscalls

Changes:

- Because we didn't realize we could use `malloc`, our original algorithm kept children alive until either their parent exited or waited on them. After clarification from the TA, we came up with a better algorithm that is very efficient.


## Task 3: File Operation Syscalls

Changes:

- We added a list of currently open files (`all_files`) in thread.h for each thread so that the file syscalls can know which files to handle. Since we have this, every time we open a file, we add to the list a struct (`file_info`) that contains all of the information the file needs so that we can access it later (pointer, id and list_elem). We also give the new file a unique file ID starting from 2 and counting up (we don't use numbers below 2 since those contain error codes). We also make sure to close all of these files when terminating the thread.
- We also ended up still closing the file at the end of `load` in thread.c and moving the setting of the executable to the file to `start_process`.


## Reflection

Roles:

- Randy: Task 1, Task 3
- Brandon: Task 2 - waiting algorithm
- Victor: Task 2 - everything else
- Alan: Task 3

What went well: Most things went smoothly, apart from some subtle synchronization issues.

What could be improved: More manual testing. Our `halt` syscall was incorrect, and only worked because the `NOT_REACHED` was reached, crashing the system.


## Student Testing Report

### Student Test 1

Description: Tests that `tell` correctly returns the position of the next byte to be 
read or written in open file fd

Overview: Creates a file "deleteme", opens the file, writes a series of 1234 random bytes
to the file, checks that tell returns 1234, seeks to 0, tests that tell returns 0, 
then seeks to 197, tests that tell returns 197. Expected output is that it correctly 
executes all the above operations, outputs the corresponding output, ends and `exit(0)`, and passes

`my-test-1.output`:
```
Copying tests/userprog/my-test-1 to scratch partition...
qemu -hda /tmp/11YuN4Q6EZ.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading..........
Kernel command line: -q -f extract run my-test-1
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  419,020,800 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 167 sectors (83 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 104 sectors (52 kB), Pintos scratch (22)
filesys: using hda2
scratch: using hda3
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'my-test-1' into the file system...
Erasing ustar archive...
Executing 'my-test-1':
(my-test-1) begin
(my-test-1) create "deleteme"
(my-test-1) open "deleteme"
(my-test-1) write "deleteme"
(my-test-1) tell "deleteme"
(my-test-1) seek "deleteme" to 0
(my-test-1) tell "deleteme"
(my-test-1) seek "deleteme" to 197
(my-test-1) tell "deleteme"
(my-test-1) close "deleteme"
(my-test-1) end
my-test-1: exit(0)
Execution of 'my-test-1' complete.
Timer: 55 ticks
Thread: 0 idle ticks, 54 kernel ticks, 1 user ticks
hda2 (filesys): 91 reads, 222 writes
hda3 (scratch): 103 reads, 2 writes
Console: 1125 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

`my-test-1.result`:
```
PASS
```

Possible bugs:

1. If kernel's implementation of tell was incorrect and didn't return 1234 after the write, then the output would be:
  ```
  (my-test-1) begin
  (my-test-1) create "deleteme"
  (my-test-1) open "deleteme"
  (my-test-1) write "deleteme"
  (my-test-1) tell "deleteme"
  (my-test-1) tell "deleteme": FAILED
  my-test-1: exit(1)
  ```

2. If kernel's implementation of seek was incorrect and didn't set next byte to 0, the output would be:
  ```
  (my-test-1) begin
  (my-test-1) create "deleteme"
  (my-test-1) open "deleteme"
  (my-test-1) write "deleteme"
  (my-test-1) tell "deleteme"
  (my-test-1) seek "deleteme" to 0
  (my-test-1) tell "deleteme"
  (my-test-1) tell "deleteme": FAILED
  my-test-1: exit(1)
  ```


### Student Test 2

`my-test-2` attempts to remove a nonexistent file already removed

we create a file then call remove on it twice. the second call should reutrn false else we fail the test case

`output`:
```
Copying tests/userprog/my-test-2 to scratch partition...
qemu -hda /tmp/18casJaY3N.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading..........
Kernel command line: -q -f extract run my-test-2
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  419,020,800 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 167 sectors (83 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 103 sectors (51 kB), Pintos scratch (22)
filesys: using hda2
scratch: using hda3
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'my-test-2' into the file system...
Erasing ustar archive...
Executing 'my-test-2':
(my-test-2) begin
(my-test-2) create "deleteme"
(my-test-2) open "deleteme"
(my-test-2) remove "deleteme"
(my-test-2) remove "deleteme"
(my-test-2) end
my-test-2: exit(0)
Execution of 'my-test-2' complete.
Timer: 56 ticks
Thread: 0 idle ticks, 54 kernel ticks, 2 user ticks
hda2 (filesys): 111 reads, 220 writes
hda3 (scratch): 102 reads, 2 writes
Console: 1004 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

`result`:
```
PASS
```

if the kernel did not immediately remove the file the first call, then our test would not work sometimes

if the kernel does not properly create the file, then our test case would not work sometimes


### Testing Experience

There wasn't too much difficulty writing the tests or running them. It was annoying having to reload the binary file onto disk every time we wanted to run the test by itself, especially since the disk would fail every so often, forcing us to remake it. It would have been nice to have a make target or a script to run individual tests.

We had an issue with the `halt` test. Even if the sycall doesn't actually work, the `NOT_REACHED` code will be reached, causing the test to pass. This suggests that the pintos testing suite would benefit from some method of determining how a process exited. When we test `exit` and `halt`, we want the process not only to exit, but to do so in a graceful fashion, e.g. not because of a fault.

Writing test cases taught us more about how the Pintos testing and build system work. This includes the Makefile structure, building tests, and loading files to disk (including the filename length limit...).
