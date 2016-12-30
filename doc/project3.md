Design Document for Project 3: File System
==========================================

## Group Members

* Brandon Pickering bpickering@berkeley.edu
* Victor Ngo vngo408@berkeley.edu
* Randy Shi beatboxx@berkeley.edu
* Alan Tong atong@berkeley.edu


## Task 1: Buffer cache

### Data structures and functions

- New files `block_cache.h` and `block_cache.c`

- New functions and data structures to implement the cache algorithm.

  `struct block_cache_entry`

  `static struct block_cache_entry *cache`

  `void block_cache_init(void)`

  `void block_cache_write_out(void)`

  `off_t block_cache_read_at(block_sector_t block, void *buffer, off_t size, off_t offset)`

  `off_t block_cache_write_at(block_sector_t block, void *buffer, off_t size, off_t offset)`

- Modify `filesys_done` to call `block_cache_write_out`

- Call `block_cache_init` in `filesys_init`

- Remove `struct inode_disk data` from `struct inode`. It counts against our cache total

- Modify `inode_create` so that it uses the cache instead of calling `block_write`. This isn't necessary for correctness, but it's probably a good idea to get the new file in the cache right away

- Remove `block_read` call from `inode_open`

- After the `free_map_release` calls in `inode_close`, the cache will now be invalid. This is actually okay since we should never attempt to read the removed blocks, but maybe invalidate those entries for good measure

- Modify `byte_to_sector` so that it looks up the `inode_disk` using the cache and reads the sector numbers from it

- Modify `inode_length` so that it reads the `inode_disk` using the cache. As an optimization, maybe keep a copy of the file length in `inode`, and be careful to update it appropriately

- Essentially just change all calls to `block_read` and `block_write` in `inode_read_at` and `inode_write_at` to instead go through the buffer cache. Also remove the bounce buffer, since we can read/write less than a full block with the cache

- Make `inode_open` and `inode_close` thread safe with respect to the `open_inodes` list

- Change all calls to `block_write` and `block_read` in `fsutil.c` to use the cache

### Algorithms

The cache is an unsorted array with 64 elements and uses LRU replacement via timestamps. Each element has the following data:

- Readers-writer lock
- Timestamp (number of ticks since machine boot at last time of access)
- Lock for the timestamp (64 bit reads/writes are not atomic)
- Dirty bit
- Sector number
- 512 byte block of data

There is one additional "mod" lock

To do a read or write operation, iterate through the list for an element with a matching sector number.

If we found a matching element:

  1. Acquire the element's RW lock as a reader/writer, depending on whether we are doing a read or a write operation
  2. Verify that the element's sector number still matches the desired one. If not, start over
  3. Set the element's timestamp to the current time (using the timestamp lock)
  4. Do the read/write operation, and set dirty bit on write
  5. Release the element's RW lock

If we did not find a matching element:

  1. Acquire the mod lock
  2. Iterate through the list again to make sure that there is still no matching element. If there is, release the mod lock and do the above steps instead
  3. Iterate through the list, looking for the element with the least recent timestamp (using each element's timestamp lock)
  4. Acquire that element's RW lock as a writer
  5. Set the element's sector number to the new sector number
  6. Set the element's timestamp to the curent time
  7. Release the mod lock
  8. If the dirty bit is set, write out the old block's data to disk and clear the bit
  9. Read in the new block's data from disk
  10. Do the desired read/write operation
  11. Release the element's RW lock

#### Notes:

- As we iterate through the list looking for the element we want to access, elements' sector numbers may be changing. This is "okay" since reading and writing these 32 bit numbers is atomic, but it may lead to false positives and false negatives

- Sector numbers are only changed by the owner of the mod lock, and a particular element's sector number is only changed by a writer on that element's RW lock. These properties allow us to detect false positives and negatives

- When a thread picks an element to kick out, it's possible for that element to be accessed or changed before the thread gets a chance to work with it. Both cases are somewhat unlikely and won't cause any correctness issues, but it may cause a non-LRU element to get kicked out accidentally

### Synchronization

We need to make sure that `open_inodes` is used in a thread-safe manner throughout `inodes.c`.

The above algorithm uses several locks and constraints to guarantee thread-safety. Edge cases can screw up the LRU policy a bit, but they won't have any effect on correctness.

The algorithm doesn't limit parallelism too much. A thread never has to wait on another thread doing reads/writes or disk operations on an unrelated sector (except in one case, where the desired sector is getting swapped out at the same time and things are lined up exactly right, but this is extremely unlikely).

In the case of a cache hit, a thread doesn't have to wait on any other thread at all, except possibly for a thread accessing the same sector.

### Justification

We considered several variants of this algorithm that use an LRU-sorted linked list instead of an array. But this increases complexity since you have to be careful about threads looking for sectors in the cache while other threads modify the list. It's possible to overcome this without locking the entire cache, but it's more complex and probably increases false negative rate. Furthermore, there really isn't that much to be gained by sorting it, since the cost of linearly searching for the LRU item is nothing compared to a disk operation.

Scanning is linear in the size of the cache, which could be improved using a hash table. But there are only 64 items in the cache, so it's not such a big deal, especially when compared to disk operations. Instead, we could try to keep the array structure contiguous in memory to make iteration as quick as possible.


## Task 2: Extensible files

### Data structures and functions

- New functions and data structures:

  `struct indirect_block_sector` - for handling indirect lookups
  
  `static bool inode_alloc (struct inode_disk *disk_inode, off_t length)` - new allocation function for the modifications
  
  `static bool inode_dealloc (struct inode *inode)` - new deallocation function for the modifications
  
- Modify `struct inode_disk` to have `block_sector_t direct_blocks[NUM_DIRECT_BLOCKS]`, `block_sector_t indirect_block` and `block_sector_t doubly_indirect_block`

- Modify `struct inode` to have a lock.

- Modify `unused` accordingly to keep `struct inode_disk` at 512 bytes.

- Modify `byte_to_sector` so that it indexes based off of the new `struct inode_disk` pointers.

- Modify `inode_create` so that it now calls `inode_alloc` instead of `free_map_allocate` since we need to handle the allocation of the pointers as well. `free_map_allocate` will be used later on in `inode_alloc`.

- Modify `inode_close` so that it now calls `inode_dealloc` instead of `free_map_release` since we need to handle the deallocation of the pointers as well. `free_map_release` will be used later on in `inode_dealloc`.

- Modify `inode_write_at` so that it checks if we're writing beyond the EOF. If we are, then we'll extend the file and write back the extended file to the cache.

- Add a new case for the inumber syscall in `syscall.c`

### Algorithms

For the direct, indirect and doubly indirect blocks, if we just make one doubly indirect block, that should already be enough to cover all 8 MB. We'll just make one indirect block and a few direct blocks and leave some space for the metadata and that should suffice.

The `indirect_block_sector` struct is for indirect mappings for the indirect pointers, and we'll create one of these whenever we need to do lookups, allocations or deallocations that involve indirect blocks. It will hold an array of blocks just like how the the `inode_disk` struct holds an array of direct blocks.

In `byte_to_sector`, we can compare the position (pos/BLOCK_SECTOR_SIZE) to increments of how many blocks of each type we have. For example, if the position is less than the number of direct blocks we have, then we know to return from the direct blocks array. Likewise, if the position is less than (the number direct blocks + the number of indirect blocks) but it is higher than the number of direct blocks, then we know to grab a block from the indirect block array.

In `inode_write_at`, use `byte_to_sector` to check if the inode does not contain data for that particular position being written to, and if it doesn't, we know it is past EOF and we'll call `inode_alloc` and then write the new extension to the cache.

For allocating, we'll do something similar to what we do in `byte_to_sector`. However, this time we'll just base the position off of the length of how much we want to extend it. We'll initialize an array of zeroes so that any gaps should just be zeroed out when we write. Then we'll grab the remaining number of sectors to be filled with `byte_to_sector`. To check how many direct blocks we're going to allocate (which should be first), we'll get the min of the remaining sector number and the number of direct blocks and allocate min times. Then we do the same by subtracting the amount we allocated from the remaining sector number to see if we still need to allocate indirect blocks by also finding the min of the remaining sector number and the number of indirect blocks per sector. Deallocation should use the same logic and we'll just free the entire inode.

####Notes

In the case that we cannot allocate the blocks properly, for inode_create we'll just have it return false and we can truncate (if possible) the number of bytes returned from inode_write_at. Otherwise we'll just return 0 if the whole extension fails.

### Synchronization

In some cases, multiple threads can access the same inode. So in `inode_reopen`, there is `inode->open_cnt++`, which is nonatomic. To handle this, we can give each inode a lock, and it should be acquired whenever the fields are being modified.
It doesn't need to be acquired when reading/writing the `inode_disk`, since the cache will take care of that.

### Justification

For the number of direct, indirect and doubly indirect blocks we'll have, we can just follow how Unix does it since one doubly indirect pointer already covers 8 MB and the rest just be any number we choose.

As for the structs, since we just need to keep the pointer data somewhere in memory and we want to keep it simple, that's why we choose to make an array of pointers for the inodes. 

For the rest, it's mostly about proper positioning and indexing so we just need to iterate through the correct amount of data and read or write at the correct position as needed. 


## Task 3: File Operation Syscalls

### Data structures and functions

- remove global `filesys_lock` from `syscall.c`

- Modify `inode_disk` so that it contains a field `bool isDir`

- Modify `struct thread` of `thread.h` by adding a field `struct dir *current_dir`

- Modify `init_thread` of `thread.c` to set the `current_dir` of `t` (the thread being initialized) to null

- Modify `init_thread` of `thread.c` so that if the thread is a process created through exec, inherit parent's `current_dir` by getting the inode of `current_dir` and opening a new dir if it's not null, otherwise set `current_dir` to `dir_open_root()`

- Modify `thread_exit` of `thread.c` so that the `current_dir` of the thread is closed on thread exit.

- Modify `filesys_create`, `filesys_open`, and `filesys_remove` by changing the line `struct dir *dir = dir_open_root ();` so that dir is set
to the `current_dir` of the current thread, this is to make sure that the lookup starts from the current directory.

- Modify `lookup` of `directory.c` to be a recursive function that supports absolute and relative path names. Specifics are mentioned in Algorithms section

- Modify `struct file_info` in `src/userprog/syscall.c` to contain a field `struct dir *dir_ptr` to be able to contain directories and a `bool isDir` to differentiate between directories and files in our fd to file mapping

- Modify `syscall_handler` of `src/userprog/syscall.c` to support chdir, mkdir, readdir, and isdir:
chdir calls `lookup` of `directory.c`

mkdir calls `lookup` to check if parent directory exists, then calls `dir_add` of `directory.c` to add the directory,
it also creates and adds directories `.` and `..` that have the same inodes as the created directory and its parent respectively.

readdir calls `dir_readdir` of `directory.c`

isdir looks at the map from fd to `file_info` (which contains either a file or directory), and sees if the `isDir` field is true or false

Each syscall uses the fd to file/directory map to get arguments for their calls to functions in `directory.c`, and performs safety checking on all the arguments passed into the syscall, just like the other syscalls

- Modify `syscall_handler` of `src/userprog/syscall.c` to make inumber, open, remove, and other syscalls work with directories:
For inumber, add a check if it's file or directory, and if it's directory get inode from `struct dir *dir_ptr` instead of from `struct file *ptr`, and get inode number from that inode

For open, add a check if it's a file or directory, and if it's a directory call `dir_open` from `directory.c` and assign a fd to it and add it to the fd map.

For remove, add a check if it's a file or directory, if it's a directory then check if it's empty besides `.` and `..` to see if it can be removed. Also, disallow removal of directories that are open or that are a process's working directory by checking 'open_cnt' of the inode of the directory.

For other syscalls that can only be done to files and not directories such as read, write, seek, and tell, add a check to see if it's a file or directory, and if it's a directory do not allow read/write

### Algorithms

- How `lookup` of `directory.c` works:
Lets say we do relative path lookup for 'A/B/C' from directory '/lol/'
We first check if directory 'A' is in '/lol/'
If true return false, else return lookup for 'B/C' from directory '/lol/A/'

If it's full path, for example lookup for '/A/B/C' from directory '/lol/'
return lookup for 'A/B/C' from root directory

- Why open, close, exec, remove now work for paths with directories too
By changing `filesys_create`, `filesys_open`, and `filesys_remove`, and `lookup` we have made open, close, exec, and remove work with directories because they call `filesys_create`, `filesys_open`, and `filesys_remove`

- How are "." and ".." supported?
Each directory has directories `.` and `..` that get added to them during mkdir


### Synchronization

There shouldn't be any synchronization errors because each process has its own files and directory pointers, so concurrent modifications of these structures cannot happen. Task 1 : Buffer Cache handles the case that multiple processes are trying to read/write from the same sector

### Justification

We thought of some alternatives such as instead of adding `.` and `..` as directories inside each directory, we could do a special case, however this seemed less simple and we'd have to somehow store the parent of each directory somewhere anyways

We thought about keeping lookup the way it is and doing path resolution in `filesys.c`, however we would have to modify `filesys_create`, `filesys_open`, and `filesys_remove` to each call a helper function to do this, and they all call lookup anyways so we decided to change `lookup` as it's simpler and makes more sense to do so in terms of abstraction.

We thought about allowing removal of directories that are open by processes, however this would require that attempts to open files (including . and
..) or create new files in a deleted directory must be disallowed, which seems more difficult to implement.


## Additional Questions

1. For write-behind, spawn a kernel thread that repeatedly calls `block_cache_write_out` and then calls `thread_sleep` for some length of time.

  For read-ahead, create a queue of sector numbers. When the cache predicts that a block is likely to be read soon, put its number on the queue. At init time, spawn a kernel thread that repeatedly blocks until the queue is nonempty and calls a function to load that sector into the cache. It's probably not sufficient to do a zero-byte read from that sector, since it might confuse the cache's read-ahead strategy.
  
2. Basically what happens is that the inode for "." is not found when we get to the `dir_lookup` part, and this causes `filesys_open` to return NULL and set the eax register to -1. Then, in ls.c, since we got -1, it will print "not found" and ls will exit with exit code 1.

`#0  dir_lookup (dir=dir@entry=0xc010707c, name=name@entry=0x804a2ed ".", inode=inode@entry=0xc0115f38) at` `../../filesys/directory.c:130`
`#1  0xc002be7c in filesys_open (name=0x804a2ed ".") at ../../filesys/filesys.c:73`
`#2  0xc002b723 in syscall_handler (f=0xc0115fb0) at ../../userprog/syscall.c:170`
`#3  0xc002195d in intr_handler (frame=0xc0115fb0) at ../../threads/interrupt.c:367`
`#4  0xc0021b5e in intr_entry () at ../../threads/intr-stubs.S:37`
`#5  0xc0115fb0 in ?? ()`
`#6  0x08049c9b in open (file=<error reading variable: can't compute CFA for this frame>) at ../lib/user/syscall.c:111`
`#7  0x0000001b in ?? ()`
