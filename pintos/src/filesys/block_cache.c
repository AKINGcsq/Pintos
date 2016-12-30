#include "filesys/block_cache.h"
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define CACHE_SIZE 64

struct block_cache_entry
  {
    struct lock access_lock;
    struct lock timestamp_lock;
    int64_t last_access;
    bool occupied;
    block_sector_t sector;
    bool dirty;
    void *data;
  };

static struct block_cache_entry cache_items[CACHE_SIZE];
static struct lock mod_lock;

int missCnt;
int hitCnt;

void
block_cache_init (void)
{
  missCnt = 0;
  hitCnt = 0;
  size_t i;
  for (i = 0; i < CACHE_SIZE; i++)
    {
      lock_init (&mod_lock);
      lock_init (&cache_items[i].access_lock);
      lock_init (&cache_items[i].timestamp_lock);
      cache_items[i].last_access = 0;
      cache_items[i].occupied = false;
      cache_items[i].dirty = false;
      cache_items[i].data = malloc (BLOCK_SECTOR_SIZE);
    }
}

void
block_cache_done (void)
{
  block_cache_write_out ();
  size_t i;
  for (i = 0; i < CACHE_SIZE; i++)
    free (cache_items[i].data);
}

void
block_cache_write_out (void)
{
  size_t i;
  for (i = 0; i < CACHE_SIZE; i++)
    {
      if (!cache_items[i].occupied) continue;
      if (!cache_items[i].dirty) continue;
      lock_acquire (&cache_items[i].access_lock);

      if (!cache_items[i].dirty)
        {
          lock_release (&cache_items[i].access_lock);
          continue;
        }
      cache_items[i].dirty = false;
      block_write (fs_device, cache_items[i].sector, cache_items[i].data);

      lock_release (&cache_items[i].access_lock);
    }
}

static void
update_timestamp (size_t index)
{
  lock_acquire (&cache_items[index].timestamp_lock);
  cache_items[index].last_access = timer_ticks ();
  lock_release (&cache_items[index].timestamp_lock);
}

static int64_t
get_timestamp (size_t index)
{
  lock_acquire (&cache_items[index].timestamp_lock);
  int64_t result = cache_items[index].last_access;
  lock_release (&cache_items[index].timestamp_lock);
  return result;
}

static size_t
select_for_eviction (void)
{
  size_t earliest_index = CACHE_SIZE;

  size_t i;
  for (i = 0; i < CACHE_SIZE; i++)
    {
      if (!cache_items[i].occupied) return i;
      if (earliest_index == CACHE_SIZE ||
          get_timestamp(i) < get_timestamp(earliest_index)) 
        earliest_index = i;
    }

  return earliest_index;
}

static size_t
find_and_access (block_sector_t sector)
{
  size_t index;

  while (true)
    {
      // If block is already in cache, find it and lock it.
      hitCnt += 1;
      for (index = 0; index < CACHE_SIZE; index++)
        {
          if (!cache_items[index].occupied) continue;
          if (cache_items[index].sector != sector) continue;

          lock_acquire (&cache_items[index].access_lock);

          // Verify that it's not a false positive.
          if (cache_items[index].sector == sector)
            {
              update_timestamp (index);
              return index;
            }
          else
            lock_release (&cache_items[index].access_lock);
        }

      lock_acquire (&mod_lock);

      // Check if false negative. If so, will probably find the element next
      // iteration
      for (index = 0; index < CACHE_SIZE; index++)
        {
          if (!cache_items[index].occupied) continue;
          if (cache_items[index].sector == sector) break;
        }
      if (index != CACHE_SIZE)
        {
          lock_release (&mod_lock);
          continue;
        }

      hitCnt -= 1;
      missCnt += 1;
      // Now the block is definitely not in the cache. Pick block to replace
      index = select_for_eviction ();
      if (index != CACHE_SIZE)
        {
          lock_acquire (&cache_items[index].access_lock);

          bool old_occupied = cache_items[index].occupied;
          block_sector_t old_sector = cache_items[index].sector;
          cache_items[index].occupied = true;
          cache_items[index].sector = sector;
          update_timestamp (index); // Before releasing mod lock for performance

          // Done modifying list structure
          lock_release (&mod_lock);

          // Write out old block, read in new block
          if (old_occupied && cache_items[index].dirty)
            block_write (fs_device, old_sector, cache_items[index].data);

          block_read (fs_device, sector, cache_items[index].data);
          cache_items[index].dirty = false;

          return index;
        }
    }
  return index;
}

void
block_cache_read_at (block_sector_t sector, void *data, off_t size,
                          off_t offset)
{
  size_t index = find_and_access (sector);
  memcpy (data, cache_items[index].data + offset, size);
  lock_release (&cache_items[index].access_lock);
}

void
block_cache_write_at (block_sector_t sector, const void *data, off_t size,
                            off_t offset)
{
  size_t index = find_and_access (sector);
  memcpy (cache_items[index].data + offset, data, size);
  cache_items[index].dirty = true;
  lock_release (&cache_items[index].access_lock);
}

int
getHitRate () 
{
  return hitCnt;
}

int
getMissRate ()
{
  return missCnt;
}

void
reset () 
{
  hitCnt = 0;
  missCnt = 0;
}
