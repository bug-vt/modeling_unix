#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static bool is_empty_line (char *line);

/* Create a new command */
struct ast_command * ast_command_create(char *cmd,
                                        char *iored_input, 
                                        char *iored_output, 
                                        bool append_to_output)
{
    if (is_empty_line (cmd))
      return NULL;

    struct ast_command *command = malloc(sizeof *command);

    command->command = cmd;
    command->iored_output = iored_output;
    command->iored_input = iored_input;
    command->append_to_output = append_to_output;
    return command;
}

/* Create an empty command line */
struct ast_command_line *
ast_command_line_create_empty(void)
{
    struct ast_command_line *cmdline = malloc(sizeof *cmdline);

    list_init(&cmdline->commands);
    return cmdline;
}

/* Create a command line with a single command */
struct ast_command_line *
ast_command_line_create(struct ast_command *cmd)
{
    struct ast_command_line *cmdline = ast_command_line_create_empty();

    list_push_back(&cmdline->commands, &cmd->elem);
    return cmdline;
}


/* Add a new command to this command line */
void
ast_command_line_add_command(struct ast_command_line *cmd_line, struct ast_command *cmd)
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
ast_command_free(struct ast_command *cmd)
{
    free(cmd);
}

/* Parse a command line based on pipe. */
struct ast_command_line * ast_parse_command_line(char * line)
{
    struct ast_command_line *cmd_line = ast_command_line_create_empty();

    char *cmd, *save_ptr;
    for (cmd = strtok_r (line, "|", &save_ptr); cmd != NULL;
         cmd = strtok_r (NULL, "|", &save_ptr))
    {
        struct ast_command *command = ast_command_create(cmd, NULL, NULL, false);
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
