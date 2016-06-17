#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STDIO_H
#include <stdio.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include "data.h"
#include "commands.h"
#include "globals.h"
#include "io.h"
#include "logger.h"
#include "a2-tgdb.h"
#include "sys_util.h"

/**
 * The info stored for each data context.
 */
struct data {
    /**
	 * The maximum size of the prompt.
	 */
#define GDB_PROMPT_SIZE 1024

    /**
	 * the state of the data context
	 */
    enum internal_state data_state;

    /**
	 * The debugger's prompt.
	 */
    char gdb_prompt[GDB_PROMPT_SIZE];

    /**
	 * The size of gdb_prompt.
	 */
    int gdb_prompt_size;

    /**
	 * What the debugger's prompt was before.
	 */
    char gdb_prompt_last[GDB_PROMPT_SIZE];
};

struct data *data_initialize(void)
{
    struct data *d = (struct data *) cgdb_malloc(sizeof (struct data));

    d->data_state = VOID;
    d->gdb_prompt_size = 0;
    d->gdb_prompt[0] = 0;
    d->gdb_prompt_last[0] = 0;

    return d;
}

void data_shutdown(struct data *d)
{
    free(d);
    d = NULL;
}

enum internal_state data_get_state(struct data *d)
{
    return d->data_state;
}

void data_set_state(struct annotate_two *a2, enum internal_state state)
{
    /* if tgdb is at an internal command, than nothing changes that
     * state unless tgdb gets to the prompt annotation. This means that
     * the internal command is done */

    a2->data->data_state = state;

    switch (a2->data->data_state) {
        case VOID:
            break;
        case AT_PROMPT:
            a2->data->gdb_prompt_size = 0;
            break;
        case USER_AT_PROMPT:
            /* Null-Terminate the prompt */
            a2->data->gdb_prompt[a2->data->gdb_prompt_size] = '\0';

            if (strcmp(a2->data->gdb_prompt, a2->data->gdb_prompt_last) != 0) {
                strcpy(a2->data->gdb_prompt_last, a2->data->gdb_prompt);
                /* Update the prompt */
                if (a2->cur_response_list) {
                    struct tgdb_response *response = (struct tgdb_response *)
                            cgdb_malloc(sizeof (struct tgdb_response));

                    response->header = TGDB_UPDATE_CONSOLE_PROMPT_VALUE;
                    response->result_id = -1;
                    response->choice.update_console_prompt_value.prompt_value =
                            cgdb_strdup(a2->data->gdb_prompt_last);
                    tgdb_list_append(a2->cur_response_list, response);
                }
            }

            a2->command_finished = 1;
            break;
        case POST_PROMPT:
            a2->data->data_state = VOID;
            break;
        case USER_COMMAND:
            break;
    }
}

void data_process(struct annotate_two *a2,
        char a, char *buf, int *n, struct tgdb_list *list)
{
    switch (a2->data->data_state) {
        case VOID:
            buf[(*n)++] = a;
            break;
        case AT_PROMPT:
            a2->data->gdb_prompt[a2->data->gdb_prompt_size++] = a;
            break;
        case USER_AT_PROMPT:
        case USER_COMMAND:
        case POST_PROMPT:
            break;
    }
}
