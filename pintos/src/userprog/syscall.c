#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <lib/user/syscall.h>
#include "filesys/block_cache.h"

static void syscall_handler (struct intr_frame *);


struct file_info
  {
    bool isDir;
    struct dir *dir_ptr;
    struct file *ptr; /* Pointer to the actual file */
    int file_id; /* This file's ID */
    struct list_elem elem; /* For pushing onto the list of files */
  };

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Finds the file_info struct associated with the given ID. */
static struct file_info *
id_to_file (int id)
{
  struct list *files = &thread_current ()->all_files;
  struct list_elem *i;
  for (i = list_begin (files); i != list_end (files); i = list_next (i))
    {
      struct file_info *curr_info = list_entry (i, struct file_info, elem);
      if (curr_info->file_id == id)
        return curr_info;
    }
  return NULL;
}

/* Closes all files. Should be called when terminating the thread. */
void
thread_close_files (void)
{
  struct list *files = &thread_current ()->all_files;
  while (!list_empty (files))
    {
      struct list_elem *e = list_pop_front (files);
      struct file_info *f = list_entry (e, struct file_info, elem);
      if (f->ptr != NULL)
        {
          if (f->isDir)
            dir_close (f->dir_ptr);
          else
            file_close (f->ptr);
          free (f);
        }
    }
}

static bool
check_valid_vaddr (const void *addr)
{
  return is_user_vaddr (addr) &&
          pagedir_get_page (thread_current () -> pagedir, addr);
}

static bool
check_valid_cstr (const char *str)
{
  if (!check_valid_vaddr (str))
    return false;
  const char *p = str;
  while (*p++ != '\0')
    if ((size_t) p % PGSIZE == 0 && !check_valid_vaddr (p))
      return false;
  return true;
}

static bool
check_valid_buffer (const void *addr, size_t size)
{
  const void *end = pg_round_down (addr + size - 1);
  const void *p;
  for (p = pg_round_down (addr); p <= end; p += PGSIZE)
    if (!check_valid_vaddr (p))
      return false;
  return true;
}

static void
ensure_valid_vaddr (struct intr_frame *f, const void *addr)
{
  if (!check_valid_vaddr (addr))
    thread_kill (f, -1);
}

static void
ensure_valid_cstr (struct intr_frame *f, uint32_t str)
{
  if (!check_valid_cstr ((char *) str))
    thread_kill (f, -1);
}

static void
ensure_valid_buffer (struct intr_frame *f, uint32_t bufaddr, uint32_t size)
{
  if (!check_valid_buffer ((void *) bufaddr, (size_t) size))
    thread_kill (f, -1);
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t *args = ((uint32_t *) f->esp);
  ensure_valid_vaddr (f, &args[0]);

  if (thread_current ()->current_dir == NULL)
    thread_current ()->current_dir = dir_open_root ();

  if (args[0] == SYS_EXIT)
    {
      ensure_valid_vaddr (f, &args[1]);
      thread_kill (f, args[1]);
    }
  else if (args[0] == SYS_PRACTICE)
    {
      ensure_valid_vaddr (f, &args[1]);
      f->eax = args[1] + 1;
    }
  else if (args[0] == SYS_HALT)
    {
      shutdown_power_off ();
    }
  else if (args[0] == SYS_EXEC)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_cstr (f, args[1]);
      tid_t child = process_execute ((const char *) args[1]);
      f->eax = child == TID_ERROR ? -1 : child;
    }
  else if (args[0] == SYS_WAIT)
    {
      ensure_valid_vaddr (f, &args[1]);
      f->eax = process_wait (args[1]);
    }
  else if (args[0] == SYS_CREATE)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_vaddr (f, &args[2]);
      ensure_valid_cstr (f, args[1]);
      f->eax = filesys_create ((char *) args[1], args[2]);
    }
  else if (args[0] == SYS_OPEN)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_cstr (f, args[1]);

      struct inode *in;
      if (!dir_lookup (thread_current ()->current_dir, (const char *) args[1], &in))
        {
          f->eax = -1;
        }
      else
        {
          static int file_id = 2; /* Start at 2 since numbers less than 2 are
                                     error codes. */

          /* Set up file_info struct to push onto the list of open files. */
          struct file_info *info = (struct file_info *)
                                      malloc (sizeof (struct file_info));

          if (!inode_isDir (in)) 
            {
              info->ptr = file_open (in);
              info->file_id = file_id;
              info->isDir = false;
            }
          else
            {
              info->dir_ptr = dir_open (in);
              info->file_id = file_id;
              info->isDir = true;
            }

          list_push_back (&thread_current ()->all_files, &info->elem);

          f->eax = file_id++;
        }
    }
  else if (args[0] == SYS_CLOSE)
    {
      ensure_valid_vaddr (f, &args[1]);

      struct file_info *main_info = id_to_file (args[1]);
      if (main_info == NULL)
        {
          f->eax = -1;
        }
      else
        {
          if (main_info->isDir)
            dir_close (main_info->dir_ptr);
          else
            file_close (main_info->ptr);
          list_remove (&main_info->elem);
          free (main_info);
        }
    }
  else if (args[0] == SYS_FILESIZE)
    {
      ensure_valid_vaddr (f, &args[1]);

      struct file_info *info = id_to_file (args[1]);
      if (info == NULL)
        {
          f->eax = -1;
        }
      else
        {
          f->eax = file_length (info->ptr);
        }
    }
  else if (args[0] == SYS_READ)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_vaddr (f, &args[2]);
      ensure_valid_vaddr (f, &args[3]);
      ensure_valid_buffer (f, args[2], args[3]);

      /* If fd is 0 then read from the keyboard. */
      if (args[1] == STDIN_FILENO)
        {
          uint32_t i;
          for (i = 0; i < args[3]; i++)
            *(uint8_t *) (args[2] + i) = input_getc ();
        }
      else if (args[1] == STDOUT_FILENO)
        {
          thread_kill (f, -1);
        }

      struct file_info *info = id_to_file (args[1]);
      if (info == NULL || info->isDir)
        f->eax = -1;
      else
        {
          f->eax = file_read (info->ptr, (void *) args[2], args[3]);
        }
    }
  else if (args[0] == SYS_WRITE)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_vaddr (f, &args[2]);
      ensure_valid_vaddr (f, &args[3]);
      ensure_valid_buffer (f, args[2], args[3]);

      if (args[1] == STDIN_FILENO)
        {
          thread_kill (f, -1);
        }
      else if (args[1] == STDOUT_FILENO)
        {
          putbuf ((char *) args[2], args[3]);
        }

      struct file_info *info = id_to_file (args[1]);
      if (info == NULL || info->isDir)
        f->eax = -1;
      else
        {
          f->eax = file_write (info->ptr, (void *) args[2], args[3]);
        }
    }

  else if (args[0] == SYS_REMOVE)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_cstr (f, args[1]);
      f->eax = filesys_remove ((const char *) args[1]);
    }
  else if (args[0] == SYS_SEEK)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_vaddr (f, &args[2]);
      struct file_info *info = id_to_file (args[1]); 
      if (info != NULL || info->isDir)
        {
          file_seek (info->ptr, args[2]);
        }
      else
        f->eax = -1;
    }
  else if (args[0] == SYS_TELL)
    {
      ensure_valid_vaddr (f, &args[1]);
      struct file_info *info = id_to_file (args[1]); 
      if (info != NULL) 
        {
          f->eax = file_tell (info->ptr);
        }
      else
        f->eax = -1;
    }
  else if (args[0] == SYS_INUMBER)
    {
      ensure_valid_vaddr (f, &args[1]);
      struct file_info *main_info = id_to_file (args[1]);
      if (main_info == NULL)
        {
          f->eax = -1;
        }
      else
        {
          if (main_info->isDir)
            f->eax = (int) inode_get_inumber (dir_get_inode (main_info->dir_ptr));
          else
            f->eax = (int) inode_get_inumber (file_get_inode (main_info->ptr));
        }
    }
  else if (args[0] == SYS_CHDIR)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_cstr (f, args[1]);
      struct inode *in;
      if (!dir_lookup (thread_current ()->current_dir, (char *) args[1], &in) || !inode_isDir (in))
        {
          f->eax = 0;
          return;
        }
      dir_close (thread_current ()->current_dir);
      thread_current ()->current_dir = dir_open (in);
      f->eax = 1;
    }
  else if (args[0] == SYS_MKDIR)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_cstr (f, args[1]);
      struct inode *in;
      if (!filesys_create ((const char *) args[1], 0) ||
          !dir_lookup (thread_current ()->current_dir, (const char *) args[1], &in))
        {
          f->eax = 0;
          return;
        }
      f->eax = 1;
      inode_setDir (in);

      struct dir *dir = dir_open (in);
      dir_add (dir, ".", inode_get_inumber (in));
      dir_add_parent (dir, thread_current ()->current_dir, (char *) args[1]);
      dir_close (dir);
    }
  else if (args[0] == SYS_READDIR)
    {
      ensure_valid_vaddr (f, &args[1]);
      ensure_valid_vaddr (f, &args[2]);
      ensure_valid_buffer (f, args[2], READDIR_MAX_LEN);
      struct file_info *info = id_to_file (args[1]);
      if (!info->isDir)
        f->eax = 0;
      else
        {
          f->eax = dir_readdir (info->dir_ptr, (char *) args[2]);
        }
    }
  else if (args[0] == SYS_ISDIR)
    {
      ensure_valid_vaddr (f, &args[1]);
      struct file_info *info = id_to_file (args[1]);
      if (info == NULL)
        f->eax = 0;
      else
        f->eax = info->isDir;
    }
  else if (args[0] == SYS_HIT)
    {
        f->eax = getHitRate ();
    }
  else if (args[0] == SYS_MISS)
    {
        f->eax = getMissRate ();
    }

  else if (args[0] == SYS_RESET_CACHE)
    {
        reset ();
        f->eax = 0;
    }
  else if (args[0] == SYS_WRITE_CNT)
    {
        f->eax = writeCnt (fs_device);
    }

  // SYS_CHDIR,                  /* Change the current directory. */
  // SYS_MKDIR,                  /* Create a directory. */
  // SYS_READDIR,                 Reads a directory entry. 
  // SYS_ISDIR,                  /* Tests if a fd represents a directory. */
  // SYS_INUMBER                 /* Returns the inode number for a fd. */
  else
    {
      printf ("System call number: %d\n", args[0]);
    }
}
