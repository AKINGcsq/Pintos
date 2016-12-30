Design Document for Project 2: User Programs
============================================

## Group Members

* Brandon Pickering bpickering@berkeley.edu
* Victor Ngo vngo408@berkeley.edu
* Randy Shi beatboxx@berkeley.edu 
* Alan Tong atong@berkeley.edu

## Task 1: Argument Passing

### Data structures and functions

We are modifying `process_execute` to detect arguments and `start_process` to manipulate the argument stack.

### Algorithms

In `process_execute`, make another copy of `file_name` called `actual_file_name` and use `strtok_r` to delimit with spaces to extract the file name to run and leave out the arguments. Then pass `actual_file_name` as the first argument to `thread_create` instead of the old `file_name` argument. 

In `start_process`, do the same thing where we delimit `file_name` with `strtok_r` and pass `actual_file_name` into `load`. We will also use `strtok_r` to push the file name and arguments onto the stack - the file name is pushed first and then the rest of the arguments are pushed onto the stack in the order they were passed in. Each time we push onto the stack we must move the stack pointer down by the length of the string pushed along with the null terminator so that `volatile` will know where the stack pointer is. If the difference between `PHYS_BASE` and the current stack pointer is greater than the maximum size of the arguments list (say 1024 bytes), then stop pushing arguments and exit the process. 

### Synchronization

We would have normally used `strtok` to handle this but `strtok_r` is thread safe since it uses an extra argument that can be created locally to keep track of its current position in the string it is tokenizing while other strings are being tokenized concurrently. `strtok` on the other hand uses a global static buffer to do this.

### Rationale

For grabbing the arguments, the best way is probably to use some sort of `strtok` function since you can just delimit by spaces. Setting up the stack should take linear time since we're just pushing arguments onto the stack so that's what we're doing.


## Task 2: Process Control Syscalls

### Data structures and functions

- New structure:

  ```
  struct child_node {
    pid_t child_pid;
    struct thread *child;
    struct thread *parent;
    int exit_status;
    struct semaphore wait_sema;
    struct list_elem elem;
  };
  ```

threads/thread.h:

- Add to `struct thread`:

  `struct list children` - list of spawned children that have not been waited on yet

  `struct child_node *child_node` - node as a part of parent's children list

threads/thread.c:

- Modify `thread_create` to initialize new fields

userprog/process.c:

- Modify `thread_exit` for wait algorithm

- Implement `process_wait`

userprog/syscall.c:

- Modify `syscall_handler`

  Add branches for the four new syscalls. 

  For `SYS_EXIT`, save `args[1]` to the thread struct

  For `SYS_HALT`, call `shutdown` (devices/shutdown.h)

  For `SYS_PRACTICE`, return `args[1] + 1`

  For `SYS_EXEC`, call `process_execute`

  For `SYS_WAIT`, call `process_wait`

- Add `static void check_valid_cstr (const char *)`

  Verify that a string argument points to valid memory

### Algorithms

#### Check if string is valid:

For a given virtual address `vaddr`, we should first make sure that it resides in user space, by calling `is_user_vaddr(vaddr)` or just `vaddr < PHYS_BASE`. 

Then we need to check that its page is currently mapped to physical memory, by checking that `*(pd + pd_no(vaddr))` is nonzero, where `pd` is the page directory, found in `struct thread`.

For a whole buffer, we can check the address for every byte in the buffer. More efficiently, we can find the two pages `start_page` and `end_page` that the start and end of the buffer are on. Then we make sure that `end_page` resides in user space, and iterate from `start_page` to `end_page`, making sure that all pages in this range are mapped. To make this work, we also need to check that the buffer does not wrap around from the top of memory to the bottom.

For a null-terminated string, we set a pointer to the start of the string. We increment this until the address is invalid (failure) or we reach a null character (success). We do not need to check validity at every step: we can check it for the start address, and then only check again when the address has a page offset of 0.

#### Waiting for children:

When a thread starts, allocate its `child_node` field if it has a parent. Initialize `child_node` fields, setting `wait_sema` to 0. Set `child_node` to `NULL` if it doesn't have a parent.

When a child exits, set the `child` field in `child_node` to NULL and raise `wait_sema`.

When waiting for a PID, look through `children` for a matching process. If none match, either that PID is not a child, or `wait` was already called for it. If we do find a child, call down on `wait_sema`.

When a parent exits or finishes waiting on a child, deallocate the child's `child_node` and orphan the child by setting `child_node` to NULL.

### Synchronization

`thread_create` is called in the parent's process before the child gets a chance to run, so we don't need any synchronization there.

Interrupts are disabled in `thread_exit`, which we might need since we are modifying the `child_node`s of other threads.

Symaphores are used for waiting, so those already provide synchronization.

### Rationale

We originally used an algorithm that kept children alive until either the parent exited or waited on them, based on a misunderstanding of what operations we had access to. The current algorithm is efficient as we never have to iterate through the list of all threads, and requires very little extra logic other than the semaphore.

## Task 3: File Operation Syscallss

### Data structures and functions

threads/thread.h:

- Add `struct file *executable` to `struct thread`

threads/thread.c:

- Add global `static struct lock filesys_lock`

- Modify `thread_init`

  Add `lock_init (&filesys_lock);` to initialize filesys_lock

- Modify `thread_create`

  Set `executable` to null

userprog/syscall.c:

- Modify `syscall_handler`

  Add branches for the new syscalls (create, remove, open, filesize, read, write, seek, tell, and close). 
  Compare `args[0]` to the syscall numbers defined in syscall-nr.h

  For `SYS_CREATE`, call `filesys_create` (filesys.c)

  For `SYS_REMOVE`, call `filesys_remove` (filesys.c)

  For `SYS_OPEN`, call `filesys_open` (filesys.c)

  For `SYS_FILESIZE`, call `file_length` (file.c)

  For `SYS_READ`, call `file_read` (file.c)

  For `SYS_WRITE`, call `file_write` (file.c)

  For `SYS_SEEK`, call `file_seek` (file.c)

  For `SYS_TELL`, call `file_tell` (file.c)

  For `SYS_CLOSE`, call `file_close` (file.c)

  Each of these calls are made using the arguments in `args`
  Before each of these calls, `filesys_lock` is acquired, and after the calls, `filesys_lock` is released

- Modify `load`

  Do not close `file` at end of `load`; instead, set `executable` of current thread to `file`

  Call `file_deny_write` (file.c) on `file` if `file` is not NULL

- Modify `process_exit`
  
  Call `file_allow_write` on `executable` of current thread if if `executable` is not NULL

  Close the file `executable` if it's not NULL


### Algorithms

Implement all the new syscalls in `syscall_handler` by calling their corresponding functions in the filesys directory, making
sure to use `filesys_lock` to make sure multiple filesys functions do not run concurrently. To ensure nobody modifies
current-running program files of user processes, create a field in `struct thread` called `executable` to save the pointer
to the executable loaded from `load`. Call `file_deny_write` on this during the creation of the process, and call
`file_allow_write` on `executable` when the process exits.

### Synchronization

We use a global lock `filesys_lock` in thread.c to make sure that non thread-safe filesys operations do not happen concurrently.

### Rationale

We used a global lock to ensure that the filesys operations do not happen concurrently because that seems to be the easiest
way to do so, and also because the project spec suggests that we do so. We implement the new syscalls in `syscall_handler` 
of userprog/syscall.c because it makes sense and it is where `SYS_EXIT` is implemented in the skeleton, so it's consistent.
We call `file_deny_write` in `load` because `load` is called during the creation of a process and deals with the file pointer
of the process's executable file. We save this file pointer in `executable` because we need to store it in order to call 
`file_allow_write` when the process ends in `process_exit`


## Additional Questions

1. In child-bad.c they pass an invalid stack pointer at line 12 where they use asm volitile. The test basically tries to use movl to load the pointer into esp and then make a system call with 0x30, and then tests to see if the code detects it and exits with -1.

2. In sc-bad-arg.c they set 0xbffffffc as the stack pointer in asm volitile (line 14) which is right at the top of the stack. Even though this is a valid pointer, the argument "i" pushed onto the stack will seep into the user address space and this is in out-of-range territory. In this case, the test will pass if the program correctly exits with -1.

3. Not all of the syscalls are tested. To mention one of them, the remove syscall is not tested and we can add tests for: removal of a non-existent file, check for a successful removal, etc.

4. GDB questions:
  1. name = `main`

    address = `0xc000e000`
    
    other threads: idle
    
    struct threads:

    ```
    pintos-debug: dumplist #0: 0xc000e000 {tid = 1, status = THREAD_RUNNING, name =
    "main", '\000' <repeats 11 times>, stack = 0xc000ee0c "\210", <incomplete sequen
    ce \357>, priority = 31, allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104
    020}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>},
     pagedir = 0x0, magic = 3446325067}

    pintos-debug: dumplist #1: 0xc0104000 {tid = 2, status = THREAD_BLOCKED, name =
    "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem
    = {prev = 0xc000e020, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034b60
     <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325
    067}
    ```


  2. Backtrace:
    ```
    #0  process_execute (file_name=file_name@entry=0xc0007d50 "args-none") at ../../userprog/process.c:32

    #1  0xc002025e in run_task (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:288

    #2  0xc00208e4 in run_actions (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:340

    #3  main () at ../../threads/init.c:133
    ```

  3. name = `args-none`

    address = `0xc010a000`

    other threads: idle, main

    struct threads:

    ```
    pintos-debug: dumplist #0: 0xc000e000 {tid = 1, status = THREAD_BLOCKED, name =
    "main", '\000' <repeats 11 times>, stack = 0xc000eebc "\001", priority = 31, all
    elem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020}, elem = {prev = 0xc0036
    554 <temporary+4>, next = 0xc003655c <temporary+12>}, pagedir = 0x0, magic = 344
    6325067}

    pintos-debug: dumplist #1: 0xc0104000 {tid = 2, status = THREAD_BLOCKED, name =
    "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem
    = {prev = 0xc000e020, next = 0xc010a020}, elem = {prev = 0xc0034b60 <ready_list>
    , next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}

    pintos-debug: dumplist #2: 0xc010a000 {tid = 3, status = THREAD_RUNNING, name =
    "args-none\000\000\000\000\000\000", stack = 0xc010afd4 "", priority = 31, allel
    em = {prev = 0xc0104020, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034
    b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446
    325067}
    ```

  4. `tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);`

  5. `0x0804870c`

  6. `#0  _start (argc=<error reading variable: can't compute CFA for this frame>, arg
    v=<error reading variable: can't compute CFA for this frame>) at ../../lib/user/
    entry.c:9`

  7. The error specified by \#6 is "can't compute CFA for this frame", CFA standing for
    Calling Frame Address. The line that it specifies is `exit (main (argc, argv));`
    This exits from main, however main does not have a calling frame, which results
    in a page fault.



