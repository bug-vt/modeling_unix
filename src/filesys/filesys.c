#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  cache_init ();
  inode_init ();
  file_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
  
  /* Spawn daemon threads dedicated to read-ahead and write-behind. */
  thread_create ("read-ahead", NICE_DEFAULT, cache_read_ahead_daemon, NULL);
  thread_create ("write-behind", NICE_DEFAULT, cache_write_behind_daemon, NULL);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  /* Write back free map first then flush the buffer cache. */
  free_map_close ();
  cache_flush ();
}

/* Create a directory named given from the path with the given
   NUM_ENTRIES.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool 
filesys_dir_create (const char *path, int num_entries)
{
  block_sector_t inode_sector = 0;

  /* Extract file name from the path. */
  char *file_name = file_name_from_path (path);
  ASSERT (file_name != NULL);
 
  /* Traverse the path until reach destination directory */
  struct dir *dir = dir_traverse_path (path, false);

  struct inode *inode = NULL;
  struct dir *new_dir;
  /* First, create new directory.
     Make sure file with the same name does not already exists. */
  bool success = (dir != NULL 
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, num_entries)
                  && dir_add (dir, file_name, inode_sector));

  if (!success && inode_sector != 0) 
    {
      free_map_release (inode_sector, 1);
      return false;
    }

  /* At this point, we know that the new directory has been created
     and its name is unique. */ 
  /* Next, add default "." and ".." entries to the 
     newly created directory. */
  success = (dir_lookup (dir, file_name, &inode)
             && (new_dir = dir_open (inode))
             && dir_add (new_dir, ".", inode_get_inumber (inode))
             && dir_add (new_dir, "..", inode_get_inumber (dir_get_inode (dir))));

  if (!success) 
    {
      dir_remove (dir, file_name);
      free_map_release (inode_sector, 1);
    }
  
  return success;
}


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size) 
{
  block_sector_t inode_sector = 0;

  /* Extract file name from the path. */
  char *file_name = file_name_from_path (path);
  ASSERT (file_name != NULL);
 
  /* Traverse the path until reach destination directory */
  struct dir *dir = dir_traverse_path (path, false);

  bool success = (dir != NULL 
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, file_name, inode_sector));

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *path)
{
  /* Extract file name from the path. */
  char *file_name = file_name_from_path (path);
  ASSERT (file_name != NULL);
 
  /* Traverse the path until reach destination directory */
  struct dir *dir = dir_traverse_path (path, false);

  struct inode *inode = NULL;

  if (dir != NULL)
    {
      /* If the path end with "/", open the rightmost directory. */
      if (strlen (file_name) == 0 && strchr(path, '/'))
        return file_open (dir_get_inode (dir));

      /* Otherwise, open a file or a directory. */
      if (!dir_lookup (dir, file_name, &inode))
        return NULL;

      return file_open (inode);
    }

  return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path) 
{
  /* Extract file name from the path. */
  char *file_name = file_name_from_path (path);
  ASSERT (file_name != NULL);
 
  /* Traverse the path until reach destination directory */
  struct dir *dir = dir_traverse_path (path, false);

  bool success = dir != NULL && dir_remove (dir, file_name);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  struct dir *root_dir;
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 0)
      || !(root_dir = dir_open_root ())
      || !dir_add (root_dir, ".", ROOT_DIR_SECTOR)
      || !dir_add (root_dir, "..", ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
