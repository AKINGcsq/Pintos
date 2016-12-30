#ifndef FILESYS_BLOCK_CACHE_H
#define FILESYS_BLOCK_CACHE_H

#include "filesys/off_t.h"
#include "devices/block.h"

void block_cache_init (void);
void block_cache_done (void);
void block_cache_write_out (void);

void block_cache_read_at (block_sector_t, void *, off_t size, off_t offset);
void block_cache_write_at (block_sector_t, const void *, off_t size,
                            off_t offset);
int getHitRate ();
int getMissRate ();
void reset ();
#endif /* filesys/block_cache.h */
