#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"

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
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_flush ();
}


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size, int is_dir) 
{
  block_sector_t inode_sector = 0;
  bool success = false;

  /* Extract file name from the path. */
  char *file_name = file_name_from_path (path);
  ASSERT (file_name != NULL);
 
  /* Traverse the path until reach destination directory */
  struct dir *dir = dir_traverse_path (path, false);

  if (is_dir)
    {
      struct inode *inode = NULL;
      struct dir *new_dir;
      success = (dir != NULL 
                 && free_map_allocate (1, &inode_sector)
                 && dir_create (inode_sector, 16)
                 && dir_add (dir, file_name, inode_sector)
                 && dir_lookup (dir, file_name, &inode)
                 && (new_dir = dir_open (inode))
                 && dir_add (new_dir, ".", inode_get_inumber (inode))
                 && dir_add (new_dir, "..", inode_get_inumber (dir_get_inode (dir))));
    }
  else
    {
      success = (dir != NULL 
                 && free_map_allocate (1, &inode_sector)
                 && inode_create (inode_sector, initial_size, is_dir)
                 && dir_add (dir, file_name, inode_sector));
    }

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  
  if (!is_dir)
    dir_close (dir);
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

      dir_close (dir);
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
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
