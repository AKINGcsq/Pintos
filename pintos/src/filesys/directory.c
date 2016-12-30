#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/block_cache.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

#define NUM_DIRECT_BLOCKS 123

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

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

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0; /* Since position 0 and 1 are reserved for . and .. */
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{ 
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (const struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

static bool
lookup_shallow (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

static bool
name_resolution (const struct dir *dir, const char *name, struct dir **retDir, char **retName)
{
  *retDir = dir_reopen (dir);
  if (name[0] == '/')
    {
      name = name + 1;
      dir_close (*retDir);
      *retDir = dir_open_root ();

      if (name[0] == '\0') {
        *retName = (char *) malloc(2);
        strlcpy(*retName, ".", 2);
        return true;
      }
    }

  size_t size = strlen (name) + 1;
  char *namecpy = (char *) malloc (size);
  strlcpy (namecpy, name, size);

  char *saveptr;
  char *namepart = strtok_r (namecpy, "/", &saveptr);
  while (namepart != NULL)
    {
      char *nextpart = strtok_r (NULL, "/", &saveptr);

      if (nextpart == NULL)
        {
          size = strlen (namepart) + 1;
          *retName = (char *) malloc (size);
          strlcpy (*retName, namepart, size);
          free (namecpy);
          return true;
        }

      struct dir_entry e;
      struct inode *in = NULL;
      if (!lookup_shallow (*retDir, namepart, &e, NULL) ||
          (in = inode_open (e.inode_sector), !inode_isDir (in)))
        {
          if (in != NULL) inode_close (in);
          dir_close (*retDir);
          free (namecpy);
          return false;
        }

      struct dir *new_dir = dir_open (in);
      dir_close (*retDir);
      *retDir = new_dir;

      namepart = nextpart;
    }

  return false;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  char *name1;
  struct dir *dir1;
  if (!name_resolution (dir, name, &dir1, &name1))
    return false;
  dir = dir1;
  name = name1;
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        free (name1);
        dir_close (dir1);
        return true;
      }
  free (name1);
  dir_close (dir1);
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  char *name1;
  struct dir *dir1;
  if (!name_resolution (dir, name, &dir1, &name1))
    return false;
  dir = dir1;
  name = name1;

  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  free (name1);
  dir_close (dir1);
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  char *name1;
  struct dir *dir1;
  if (!name_resolution (dir, name, &dir1, &name1))
    return false;
  dir = dir1;
  name = name1;

  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    goto done;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  free (name1);
  dir_close (dir1);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  char *name1;
  struct dir *dir1;
  if (!name_resolution (dir, name, &dir1, &name1))
    return false;
  dir = dir1;
  name = name1;

  dir1 = NULL;

  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  if (e.inode_sector == ROOT_DIR_SECTOR)
    goto done;


  if (inode_isDir (inode)) 
    {
      dir1 = dir_open (inode);
      char name1[NAME_MAX + 1];
      if (dir_readdir (dir1, name1)) 
        {
          goto done;
        }

      if (!inode_remove_if_not_open (inode))
        {
          goto done;
        }
    }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  if (!inode_isDir (inode))
    inode_remove (inode);
  success = true;

 done:
  free (name1);
  dir_close (dir);

  if (dir1 != NULL)
    dir_close (dir1);
  else
    inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use && strcmp (e.name, ".") && strcmp (e.name, ".."))
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

void 
dir_add_parent (struct dir *dir, struct dir *search, const char *name) 
{
  struct dir *parDir;
  char *fname;
  if (!name_resolution (search, name, &parDir, &fname))
    return;
  dir_add (dir, "..", inode_get_inumber (parDir->inode));
  dir_close (parDir);
  free (fname);
}
