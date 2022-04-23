#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"
#include "filesys/directory.h"
#include "filesys/pipe.h"

enum _file_type {STDIN, STDOUT, REG, DIR, PIPE};
typedef enum _file_type file_type;

struct inode;
struct pipe;

void file_init (void);

/* Opening and closing files. */
struct file *file_open_console (file_type);
struct file *file_open (struct inode *);
struct file *file_reopen (struct file *);
void file_close (struct file *);
struct inode *file_get_inode (struct file *);
struct file *file_dup (struct file *);

/* Reading and writing. */
off_t file_read (struct file *, void *, off_t);
off_t file_read_at (struct file *, void *, off_t size, off_t start);
off_t file_write (struct file *, const void *, off_t);
off_t file_write_at (struct file *, const void *, off_t size, off_t start);

/* Preventing writes. */
void file_deny_write (struct file *);
void file_allow_write (struct file *);

/* File position. */
void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);

/* Extract file name from path. */
char *file_name_from_path (const char *path);

/* Used when file is directory. */
struct dir *file_get_directory (struct file *);

/* Used for pipe. */
bool file_pipe_ends (struct pipe *, struct file **read_end, struct file **write_end);

#endif /* filesys/file.h */
