Final Report for Project 3: File System
=======================================

## Task 1: Buffer cache

Changes:

- Instead of using an RW lock, we used just a regular lock, at our TA's suggestion (for simplicity)


## Task 2: Extensible files

Changes:

- Added some additional synchronization, particularly in `write_at`
- Added a `min` function to help clean up code.
- Instead of modifying `unused`, we instead delete it and make 123 direct blocks to ensure that `inode_disk` is 512 bytes.



## Task 3: Subdirectories

Changes:

- Instead of setting the thread's current working directory to the root directory by default in `init_thread` of `thread.c`, we set it to NULL by default and when the thread does a syscall, we check if its cwd is NULL, if so we set it to the root directory in `syscall.c`. This was necessary because the first thread is initialized before `dir_open_root` could be called, since some setup still hadn't taken place.
- Instead of only changing `lookup` of `directory.c` to support path resolution in names, we found it necessary to implement a function `name_resolution` in `directory.c`, which is called by every function in `directory.c` that requires path resolution.
- We made sure to make the root directory have correct "." and ".." entries


## Reflection

Roles:

- Brandon: Task 1
- Randy: Task 2
- Alan: Task 3
- Victor: Testing

## Student Testing Report

### Student Test 1

Description: Tests that that the cache is effective in reducing load by measuring the hit and miss rates

Overview: Creates a file "deleteme", opens the file, writes a series of 30000 random bytes
to the file, checks the hit and miss rates, checks that tell returns 30000, seeks to 0, tests that tell returns 0, 
then read file for 30000 bytes, check the hit and miss rates. Expected output is that it correctly 
executes all the above operations, outputs the corresponding output, ends and `exit(0)`, and passes

`my-test-1.output`:
```
Copying tests/filesys/base/my-test-1 to scratch partition...
qemu -hda /tmp/wvzCozMWFL.dsk -m 4 -net none -nographic -monitor null
PiLo hda1^M
Loading...........^M
Kernel command line: -q -f extract run my-test-1
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  471,040,000 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 185 sectors (92 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 110 sectors (55 kB), Pintos scratch (22)
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
(my-test-1) seek "deleteme" to 0
(my-test-1) tell "deleteme"
(my-test-1) read "deleteme"
(my-test-1) end
my-test-1: exit(0)
Execution of 'my-test-1' complete.
Timer: 66 ticks
Thread: 0 idle ticks, 63 kernel ticks, 3 user ticks
hda2 (filesys): 333 reads, 304 writes
hda3 (scratch): 109 reads, 2 writes
Console: 1062 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

`my-test-1.result`:
```
PASS
```

Possible bugs:

1. If kernel's implementation did not properly cache sectors, then the rates would not improve the the test would fail:
  ```
  (my-test-1) begin
  (my-test-1) create "deleteme"
  (my-test-1) open "deleteme"
  (my-test-1) write "deleteme"
  (my-test-1) seek "deleteme" to 0
  (my-test-1) tell "deleteme"
  (my-test-1) read "deleteme"
  (my-test-1) Cache Ineffective: FAILED
  my-test-1: exit(1)
  ```

2. If kernel's implementation switched to another process that wiped the cache, we would get the same results as above


### Student Test 2

`my-test-2` tests the cache's ability to coalesce writes to the same sector

Description: write a large file, 64kb, then read it, check that the cache coalesces writes and that the writes be in the order of 128

Overview: Creates a file "deleteme", opens the file, writes a series of 64000 random bytes
to the file, checks that tell returns 64000, seeks to 0, tests that tell returns 0, 
then read file for 64000 bytes, check that the write count is greater than 64 and less than 256. Expected output is that it correctly 
executes all the above operations, outputs the corresponding output, ends and `exit(0)`, and passes

`output`:
```
– Copying tests/filesys/base/my-test-2 to scratch partition...
qemu -hda /tmp/p_1YS3AxvO.dsk -m 4 -net none -nographic -monitor null
PiLo hda1^M
Loading...........^M
Kernel command line: -q -f extract run my-test-2
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  104,755,200 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 185 sectors (92 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 110 sectors (55 kB), Pintos scratch (22)
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
(my-test-2) write "deleteme"
(my-test-2) tell "deleteme"
(my-test-2) read "deleteme"
(my-test-2) end
my-test-2: exit(0)
Execution of 'my-test-2' complete.
Timer: 69 ticks
Thread: 0 idle ticks, 58 kernel ticks, 11 user ticks
hda2 (filesys): 630 reads, 474 writes
hda3 (scratch): 109 reads, 2 writes
Console: 1030 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

`result`:
```
PASS
```

if the kernel did not properly coalesce sectors, then the test would fail:

  (my-test-2) begin
  (my-test-2) create "deleteme"
  (my-test-2) open "deleteme"
  (my-test-2) write "deleteme"
  (my-test-2) tell "deleteme"
  (my-test-2) read "deleteme"
  (my-test-2) Does not Coalesce Writes: FAILED
  my-test-2: exit(1)

if the kernel write was not functioning then we would have errored earlier:

  (my-test-2) begin
  (my-test-2) create "deleteme"
  (my-test-2) open "deleteme"
  (my-test-2) write "deleteme": FAILED
  my-test-2: exit(1)

### Testing Experience

Writing tests helped solidify how programs can access resources via syscalls. This was necessary to write the tests to check the cache rates and write counts. Again, it was annoying having to reload the binary file onto disk every time we wanted to run the test by itself, especially since the disk would fail every so often, forcing us to remake it. It would have been nice to have a make target or a script to run individual tests. The tests weren't difficult to write after understanding the architecture of pintos and were a pleasant experience writing.
