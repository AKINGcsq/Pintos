#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "threads/synch.h"
#include "filesys/block_cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_DIRECT_BLOCKS 123  /* To help make inode_disk exactly 512 bytes. */
#define NUM_SECTOR_INDIRECT_BLOCKS 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    /* Index structure pointers */
    block_sector_t direct_blocks[NUM_DIRECT_BLOCKS];
    block_sector_t indirect_block;
    block_sector_t doubly_indirect_block;

    uint8_t isDir;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
  };

/* For handling indirect lookups. */
struct indirect_block_sector 
  {
    block_sector_t indirect_blocks[NUM_SECTOR_INDIRECT_BLOCKS];
  };

static bool inode_alloc (struct inode_disk *disk_inode, off_t length);
static bool inode_dealloc (struct inode *inode);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the minimum of X and Y. */
static inline size_t
min (size_t x, size_t y)
{
  return x < y ? x : y;
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock inode_lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  struct inode_disk *data = calloc (1, sizeof (struct inode_disk));
  block_cache_read_at (inode->sector, data, BLOCK_SECTOR_SIZE, 0);

  if (pos < data->length)
    {
      off_t index = pos / BLOCK_SECTOR_SIZE;  /* Block index. */
      off_t lim = NUM_DIRECT_BLOCKS;
      off_t base;
      block_sector_t to_return;

      /* Check direct blocks first. */
      if (index < lim)
        {
          to_return = data->direct_blocks[index];
          free (data);
          return to_return;
        }
      base = lim;

      /* Check the indirect block next. */
      lim += NUM_SECTOR_INDIRECT_BLOCKS;
      if (index < lim)
        {
          block_cache_read_at (data->indirect_block, &to_return,
                                4, 4*(index - base));
          free (data);
          return to_return;
        }
      base = lim;

      /* Check the doubly indirect block next. */
      lim += NUM_SECTOR_INDIRECT_BLOCKS * NUM_SECTOR_INDIRECT_BLOCKS;
      if (index < lim)
        {
          /* Create different level indices for accessing the doubly indirect block. */
          off_t i_1 = (index - base) / NUM_SECTOR_INDIRECT_BLOCKS;
          off_t i_2 = (index - base) % NUM_SECTOR_INDIRECT_BLOCKS;

          block_sector_t ibs;
          block_cache_read_at (data->doubly_indirect_block, &ibs, 4, 4*i_1);
          block_cache_read_at (ibs, &to_return, 4, 4*i_2);

          free (data);
          return to_return;
        }
    }

  free(data);
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&open_inodes_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->isDir = 0;
      if (inode_alloc (disk_inode, disk_inode->length)) 
        {
          block_cache_write_at (sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire (&open_inodes_lock);

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          lock_release (&open_inodes_lock);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    {
      lock_release (&open_inodes_lock);
      return NULL;
    }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->inode_lock);

  lock_release (&open_inodes_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    {
      lock_acquire (&inode->inode_lock);
      inode->open_cnt++;
      lock_release (&inode->inode_lock);
    }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Handles the allocation of indirect pointers.
   The level is deterined by D. */
static bool
inode_alloc_indirect (block_sector_t *indirect_block, size_t remaining_sectors, int d)
{
  /* For zeroing out the allocation. */
  static char zeroes[BLOCK_SECTOR_SIZE];

  if (d == 0)
    {
      if (!*indirect_block)
        {
          if (!free_map_allocate (1, indirect_block))
            return false;
          block_cache_write_at (*indirect_block, zeroes, BLOCK_SECTOR_SIZE, 0);
        }
      return true;
    }
  else
    {
      struct indirect_block_sector ibs;
      if (!*indirect_block)
        {
          free_map_allocate (1, indirect_block);
          block_cache_write_at (*indirect_block, zeroes, BLOCK_SECTOR_SIZE, 0);
        }
      block_cache_read_at (*indirect_block, &ibs, BLOCK_SECTOR_SIZE, 0);

      size_t unit = d == 1 ? 1 : NUM_SECTOR_INDIRECT_BLOCKS;
      size_t lim = DIV_ROUND_UP (remaining_sectors, unit);

      size_t i;
      for (i = 0; i < lim; i++)
        {
          size_t amt_left = min (remaining_sectors, unit);
          if (!inode_alloc_indirect (&ibs.indirect_blocks[i], amt_left, d - 1))
            return false;
          remaining_sectors -= amt_left;
        }

      block_cache_write_at (*indirect_block, &ibs, BLOCK_SECTOR_SIZE, 0);
      return true;
    }
}

/* Allocate to DISK_INODE so that it can hold at least LENGTH bytes. */
static bool
inode_alloc (struct inode_disk *disk_inode, off_t length)
{
  size_t remaining_sectors = bytes_to_sectors (length);
  size_t lim = min (remaining_sectors, NUM_DIRECT_BLOCKS);

  /* Allocate direct blocks first. */
  size_t i;
  for (i = 0; i < lim; i++)
    {
      if (!disk_inode->direct_blocks[i])
        {
          if (!free_map_allocate (1, &disk_inode->direct_blocks[i]))
            return false;
          /* For zeroing out the allocation. */
          static char zeroes[BLOCK_SECTOR_SIZE];
          block_cache_write_at (disk_inode->direct_blocks[i], zeroes, BLOCK_SECTOR_SIZE, 0);
        }
    }
  remaining_sectors -= lim;
  if(!remaining_sectors)
    return true;

  /* Allocate the indirect block next. */
  lim = min(remaining_sectors, NUM_SECTOR_INDIRECT_BLOCKS);
  if (!inode_alloc_indirect (&disk_inode->indirect_block, lim, 1))
    return false;
  remaining_sectors -= lim;
  if (!remaining_sectors)
    return true;

  /* Allocate the doubly indirect block next. */
  lim = min(remaining_sectors, NUM_SECTOR_INDIRECT_BLOCKS * NUM_SECTOR_INDIRECT_BLOCKS);
  if (!inode_alloc_indirect (&disk_inode->doubly_indirect_block, lim, 2))
    return false;
  remaining_sectors -= lim;
  if(!remaining_sectors)
    return true;

  return false;
}

/* Handles the deallocation of indirect pointers.
   The level is deterined by D. */
static void
inode_dealloc_indirect (block_sector_t indirect_block, size_t remaining_sectors, int d)
{
  if (d > 0)
    {
      struct indirect_block_sector ibs;
      block_cache_read_at (indirect_block, &ibs, BLOCK_SECTOR_SIZE, 0);

      size_t unit = d == 1 ? 1 : NUM_SECTOR_INDIRECT_BLOCKS;
      size_t lim = DIV_ROUND_UP (remaining_sectors, unit);

      size_t i;
      for (i = 0; i < lim; i++)
        {
          size_t amt_left = min (remaining_sectors, unit);
          inode_dealloc_indirect (ibs.indirect_blocks[i], amt_left, d - 1);
          remaining_sectors -= amt_left;
        }
    }
  free_map_release (indirect_block, 1);
}

/* Deallocates DISK_INODE. */
static bool
inode_dealloc (struct inode *inode)
{
  struct inode_disk *data = calloc (1, sizeof (struct inode_disk));
  block_cache_read_at (inode->sector, data, BLOCK_SECTOR_SIZE, 0);

  size_t remaining_sectors = bytes_to_sectors (data->length);
  size_t lim = min (remaining_sectors, NUM_DIRECT_BLOCKS);

  /* Direct blocks. */
  size_t i;
  for (i = 0; i < lim; i++)
    free_map_release (data->direct_blocks[i], 1);
  remaining_sectors -= lim;

  /* Indirect block. */
  lim = min (remaining_sectors, NUM_SECTOR_INDIRECT_BLOCKS);
  if (lim)
    {
      inode_dealloc_indirect (data->indirect_block, lim, 1);
      remaining_sectors -= lim;
    }

  /* Doubly indirect block. */
  lim = min (remaining_sectors, NUM_SECTOR_INDIRECT_BLOCKS * NUM_SECTOR_INDIRECT_BLOCKS);
  if (lim)
    {
      inode_dealloc_indirect (data->doubly_indirect_block, lim, 2);
      remaining_sectors -= lim;
    }

  free (data);
  return true;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire (&open_inodes_lock);
      list_remove (&inode->elem);
      lock_release (&open_inodes_lock);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_dealloc (inode);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Delete inode if it is not opened by someone other than the caller */
bool
inode_remove_if_not_open (struct inode *inode)
{
  ASSERT (inode != NULL);
  lock_acquire (&inode->inode_lock);
  if (inode->open_cnt > 1)
    {
      lock_release (&inode->inode_lock);
      return false;
    }
  inode->removed = true;
  lock_release (&inode->inode_lock);
  return true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      lock_acquire (&inode->inode_lock);
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      lock_release (&inode->inode_lock);
      if (sector_idx == (block_sector_t) -1) break;

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read data into caller's buffer */
      block_cache_read_at (sector_idx, buffer + bytes_read, chunk_size, sector_ofs);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  lock_acquire (&inode->inode_lock);

  /* We're beyond the EOF - extend the file. */
  if (byte_to_sector (inode, offset + size - 1) == -1u)
    {
      struct inode_disk *data = calloc (1, sizeof (struct inode_disk));
      block_cache_read_at (inode->sector, data, BLOCK_SECTOR_SIZE, 0);
      if (!inode_alloc (data, offset + size)) 
        {
          free (data);
          return 0;
        }
      data->length = offset + size;
      block_cache_write_at (inode->sector, data, BLOCK_SECTOR_SIZE, 0);
      free (data);
    }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      ASSERT (sector_idx != (block_sector_t) -1);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Write data to disk */
      block_cache_write_at (sector_idx, buffer + bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  lock_release (&inode->inode_lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  off_t to_return;
  block_cache_read_at (inode->sector, &to_return, 4, offsetof(struct inode_disk, length));
  return to_return;
}

bool
inode_isDir (const struct inode *inode)
{
  uint8_t isDir;
  block_cache_read_at (inode->sector, &isDir, 1, offsetof(struct inode_disk, isDir)); 
  return isDir;
}

void 
inode_setDir (const struct inode *inode)
{
  uint8_t isDir = 1;
  block_cache_write_at (inode->sector, &isDir, 1, offsetof(struct inode_disk, isDir)); 
}
