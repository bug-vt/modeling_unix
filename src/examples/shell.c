#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include "parser.h"
#include "syscall_wrapper.h"

static void read_line (char line[], size_t);
static bool backspace (char **pos, char line[]);
static void move_cursor (char c, char **pos, char line[], size_t size);
static bool built_in_command (char *cmd);

int
main (void)
{
  printf ("Shell starting...\n");
  for (;;) 
    {
      char command[80];

      /* Read command. */
      printf ("> ");
      read_line (command, sizeof command);

      /* Parse command line group based on its placement with pipes. */
      struct ast_command_line *cline = ast_parse_command_line(command);
      if (list_empty(&cline->commands)) /* Empty input from user. */
        {    
          ast_command_line_free(cline);
          continue;
        }

      // Setting up pipes
      int cmd_count = list_size (&cline->commands);
      int cmd_index = 0;
      int pipes[cmd_count][2];
      for (int i = 0; i < cmd_count; i++) 
        Pipe (pipes[i]);

      int pids[cmd_count];
      // Iterate over each command.
      for (struct list_elem *e = list_begin (&cline->commands);
           e != list_end (&cline->commands); ) 
        {
          struct ast_command *cmd = list_entry (e, struct ast_command, elem);
          
          if (built_in_command (cmd->command)) 
            {
              e = list_remove (e);
              ast_command_free (cmd); 
              continue;
            }

          // running non-built-in command 
          pids[cmd_index] = Fork ();
          /* Child */
          if (pids[cmd_index] == 0) 
            { 
              // I/O piping 
              if (list_size (&cline->commands) > 1) 
                {
                  /* First command in command line */
                  if (e == list_begin (&cline->commands)) 
                    Dup2 (pipes[cmd_index][WRITE_END], STDOUT_FILENO);
                  /* Intermidiate command */
                  else if (e == list_rbegin (&cline->commands)) 
                    Dup2 (pipes[cmd_index - 1][READ_END], STDIN_FILENO);
                  /* Last command */
                  else 
                    {
                      Dup2 (pipes[cmd_index][WRITE_END], STDOUT_FILENO);
                      Dup2 (pipes[cmd_index - 1][READ_END], STDIN_FILENO);
                    }
                }
              for (int i = 0; i < cmd_count; i++) 
                {
                  close (pipes[i][READ_END]);
                  close (pipes[i][WRITE_END]);
                }

              // I/O redirection
              if (cmd->iored_input) 
                {
                  close(STDIN_FILENO);
                  Open(cmd->iored_input);
                }
              if (cmd->iored_output) 
                {
                  close(STDOUT_FILENO);
                  create (cmd->iored_output, 0);
                  int fd = Open(cmd->iored_output);
                  if (cmd->append_to_output)
                    seek (fd, filesize (fd));
                }

              exec2 (cmd->command);
            }
          /* Parent */
          /* Move on to next command in command line. */
          e = list_next(e);
          cmd_index++;
        }
      /* parent */
      for (int i = 0; i < cmd_count; i++) 
        {
          close (pipes[i][READ_END]);
          close (pipes[i][WRITE_END]);
        }

      /* Wait for all child to finish. */
      for (int i = 0; i < cmd_count; i++)
        wait(pids[i]);
  
      ast_command_line_free(cline);
    }

  printf ("Shell exiting.");
  return EXIT_SUCCESS;
}

/* Reads a line of input from the user into LINE, which has room
   for SIZE bytes.  Handles backspace and Ctrl+U in the ways
   expected by Unix users.  On return, LINE will always be
   null-terminated and will not end in a new-line character. */
static void
read_line (char line[], size_t size) 
{
  char *pos = line;
  for (;;)
    {
      char c;
      read (STDIN_FILENO, &c, 1);

      switch (c) 
        {
        case '\r':
          *pos = '\0';
          putchar ('\n');
          return;

        case 127:   /* Back space */
          backspace (&pos, line);
          break;

        case '\b':  /* Another back space? */
          backspace (&pos, line);
          break;

        case ('U' - 'A') + 1:       /* Ctrl+U. */
          while (backspace (&pos, line))
            continue;
          break;

        case 27:    /* Left or right arrow key */
          move_cursor (c, &pos, line, size);
          break;

        default:
          /* Add character to line. */
          if (pos < line + size - 1) 
            {
              putchar (c);
              *pos++ = c;
            }
          break;
        }
    }
}

/* If *POS is past the beginning of LINE, backs up one character
   position.  Returns true if successful, false if nothing was
   done. */
static bool
backspace (char **pos, char line[]) 
{
  if (*pos > line)
    {
      /* Back up cursor, overwrite character, back up
         again. */
      printf ("\b \b");
      (*pos)--;
      return true;
    }
  else
    return false;
}

static void 
move_cursor (char c, char **pos, char line[], size_t size)
{
  read (STDIN_FILENO, &c, 1);
  read (STDIN_FILENO, &c, 1);
  if (*pos > line && *pos < line + size  - 1)
    {
      if (c == 67)  /* Right arrow */
        {
          printf ("%c[C", 27); 
          (*pos)++;
        }
      else if (c == 68) /* Left arrow */
        {
          printf ("\b");
          (*pos)--;
        }
    }
}

/* Checks if the given command match with the built-in command.
   In that case, execute built-in. */
static bool 
built_in_command(char *command)
{
  bool is_built_in = true;

  char *cmd_copy = malloc (strlen (command) + 1);
  strlcpy (cmd_copy, command, strlen (command) + 1);
 
  /* Truncate any white space. */
  char *cmd, *save_ptr;
  cmd = strtok_r (cmd_copy, " \t\r\n", &save_ptr);
  /* Execute command. */
  if (!strcmp (cmd, "exit"))
    exit (0); 
  else if (!strcmp (cmd, "cd")) 
    {
      char *path = strtok_r (NULL, " \t\r\n", &save_ptr);
      if (!chdir (path))
        printf ("\"%s\": chdir failed\n", command + 3);
    }
  else
    is_built_in = false;

  free (cmd_copy);
  return is_built_in;
}
