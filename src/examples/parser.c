#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static bool is_empty_line (char *line);
static void eval_redirection (char *line, char **iored_in, char **iored_out);

/* Create a new command */
struct ast_command * 
ast_command_create(char *cmd, char *iored_in, char *iored_out, bool append_to_out)
{
    if (is_empty_line (cmd))
      return NULL;

    struct ast_command *command = malloc(sizeof *command);

    char *save_ptr;
    command->command = cmd;
    command->iored_output = iored_out ? strtok_r (iored_out, " \t\r\n", &save_ptr) : NULL;
    command->iored_input = iored_in ? strtok_r (iored_in, " \t\r\n", &save_ptr) : NULL;
    command->append_to_output = append_to_out;
    return command;
}

/* Create an empty command line */
struct ast_command_line *
ast_command_line_create_empty (void)
{
    struct ast_command_line *cmdline = malloc(sizeof *cmdline);

    list_init(&cmdline->commands);
    return cmdline;
}

/* Create a command line with a single command */
struct ast_command_line *
ast_command_line_create (struct ast_command *cmd)
{
    struct ast_command_line *cmdline = ast_command_line_create_empty();

    list_push_back(&cmdline->commands, &cmd->elem);
    return cmdline;
}


/* Add a new command to this command line */
void
ast_command_line_add_command (struct ast_command_line *cmd_line, struct ast_command *cmd)
{
    list_push_back(&cmd_line->commands, &cmd->elem);
}

  
/* Print ast_command structure to stdout */
void
ast_command_print(struct ast_command *cmd)
{
    printf(" Commands: %s\n", cmd->command);

    if (cmd->iored_output)
        printf("  the stdout of the last command %ss to %s\n", 
                cmd->append_to_output ? "append" : "write",
                cmd->iored_output);

    if (cmd->iored_input)
        printf("  stdin of the first command reads from %s\n", cmd->iored_input);
}

/* Print ast_command_line structure to stdout */
void 
ast_command_line_print(struct ast_command_line *cmdline)
{
    printf("Command line\n");
    for (struct list_elem * e = list_begin (&cmdline->commands); 
         e != list_end (&cmdline->commands); 
         e = list_next (e)) {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);

        printf(" ------------- \n");
        ast_command_print(cmd);
    }
    printf("==========================================\n");
}

/* Deallocation functions. */
void 
ast_command_line_free(struct ast_command_line *cmdline)
{
    for (struct list_elem * e = list_begin(&cmdline->commands); 
         e != list_end(&cmdline->commands); ) 
    {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);
        e = list_remove(e);
        ast_command_free(cmd);
    }
    free(cmdline);
}

void 
ast_command_free (struct ast_command *cmd)
{
    free(cmd);
}

/* Parse a command line based on pipe. */
struct ast_command_line * 
ast_parse_command_line (char * line)
{
    struct ast_command_line *cmd_line = ast_command_line_create_empty();

    char *cmd, *save_ptr;
    for (cmd = strtok_r (line, "|", &save_ptr); cmd != NULL;
         cmd = strtok_r (NULL, "|", &save_ptr))
    {
        char *iored_in = NULL;
        char *iored_out = NULL;
        bool append_out = strstr (cmd, ">>");
        eval_redirection (cmd, &iored_in, &iored_out); 

        struct ast_command *command = ast_command_create(cmd, iored_in, iored_out, append_out);
        if (command)
          ast_command_line_add_command(cmd_line, command);
    }

    return cmd_line;
}

/* Check if the given line only contains white spaces. */
static bool
is_empty_line (char *line)
{
  for (size_t i = 0; i < strlen (line); i++)
    {
      if (!isspace (line[i]))
        return false;
    }
  return true;
}

/* Evaluate the given command and find redirections if any. */
static void 
eval_redirection (char *cmd, char **iored_in, char **iored_out)
{
    char *in = strstr (cmd, "<");
    char *out = strstr (cmd, ">");

    char *save_ptr;
    strtok_r (cmd, "<>", &save_ptr);
    char *first = strtok_r (NULL, "<>>", &save_ptr);
    char *second = strtok_r (NULL, "<>>", &save_ptr);

    /* Output redirection. Output come first. */
    if (out)
      {
        *iored_out = first;
        if (in > out)
          *iored_in = second;
      }
    /* input redirection. input come first. */
    if (in && *iored_in == NULL)
      {
        *iored_in = first;
        if (in < out)
          *iored_out = second;
      }
}
