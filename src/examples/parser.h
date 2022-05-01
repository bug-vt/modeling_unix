#ifndef EXAMPLE_PARSER_H
#define EXAMPLE_PARSER_H

#include <kernel/list.h>

/* Forward declarations. */
struct ast_command;
struct ast_command_line;

/* A command line may contain multiple commands. */
struct ast_command_line {
    struct list/* <ast_pipeline> */ commands;        /* List of commands */
};

/* A command is part of a command line. */
struct ast_command {
    char *command;           /* String that represent one command. */
    char *iored_input;       /* If non-NULL, first command should read from
                                file 'iored_input' */
    char *iored_output;      /* If non-NULL, last command should write to
                                file 'iored_output' */
    bool append_to_output;   /* True if user typed >> to append */
    struct list_elem elem;   /* Link element. */
};

/* Create a new command */
struct ast_command * ast_command_create(char *cmd,
                                        char *iored_input, 
                                        char *iored_output, 
                                        bool append_to_output);

/* Create an empty command line */
struct ast_command_line * ast_command_line_create_empty(void);

/* Create a command line with a single command */
struct ast_command_line * ast_command_line_create(struct ast_command *);

/* Add a command to the command line */
void ast_command_line_add_command(struct ast_command_line *, struct ast_command *);

/* Deallocation functions */
void ast_command_line_free(struct ast_command_line *);
void ast_command_free(struct ast_command *);

/* Print functions */
void ast_command_print(struct ast_command *);
void ast_command_line_print(struct ast_command_line *);

/* Parse a command line. */
struct ast_command_line * ast_parse_command_line(char * line);

#endif
