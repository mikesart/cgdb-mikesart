#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STDIO_H
#include <stdio.h>
#endif /* HAVE_STDIO_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#include "state_machine.h"
#include "commands.h"
#include "logger.h"
#include "annotate_two.h"
#include "sys_util.h"
#include "ibuf.h"
#include "mi_gdb.h"

/* This package looks for annotations coming from gdb's output.
 * The program that is being debugged does not have its output pass
 * through here. So only gdb's output is filtered here.
 *
 * This is a simple state machine that is looking for annotations
 * in gdb's output. Annotations are of the form
 * '\n\032\032annotation\n'
 * However, on windows \n gets mapped to \r\n So we take account
 * for that by matching the form
 * '\r+\n\032\032annotation\r+\n'
 * 
 * When an annotation is found, this unit passes the annotation to the 
 * annotate unit and this unit is free of all responsibility :)
 */

enum state {
    SM_CGDB_GDBMI,
    SM_DATA,                       /* data from debugger */
    SM_NEW_LINE,                   /* got '\n' */
    SM_CONTROL_Z,                  /* got first ^Z '\032' */
    SM_ANNOTATION,                 /* got second ^Z '\032' */
    SM_NL_DATA                     /* got a nl at the end of annotation */
};

int mi_get_result_record(struct ibuf *buf, char **lstart, int *id);

/** The data needed to parse the output of GDB. */
struct state_machine {

    /** The maximum size of the prompt. */
#define GDB_PROMPT_SIZE 1024

    /** the state of the data context */
    enum internal_state data_state;

    /** The debugger's prompt. */
    char gdb_prompt[GDB_PROMPT_SIZE];

    /** The size of gdb_prompt. */
    int gdb_prompt_size;

    /** What the debugger's prompt was before. */
    char gdb_prompt_last[GDB_PROMPT_SIZE];

    struct ibuf *cgdb_gdbmi_buffer;

    /** Annotations will be stored here. */
    struct ibuf *tgdb_buffer;

    /** The state of the annotation parser ( current context ). */
    enum state tgdb_state;

    /** Is a misc prompt command be run. */
    unsigned short misc_prompt_command;
};

struct state_machine *state_machine_initialize(void)
{
    struct state_machine *sm =
            (struct state_machine *) cgdb_malloc(sizeof (struct state_machine));

    sm->data_state = VOID;
    sm->gdb_prompt_size = 0;
    sm->gdb_prompt[0] = 0;
    sm->gdb_prompt_last[0] = 0;
    sm->cgdb_gdbmi_buffer = ibuf_init();
    sm->tgdb_buffer = ibuf_init();
    sm->tgdb_state = SM_DATA;
    sm->misc_prompt_command = 0;

    return sm;
}

void state_machine_shutdown(struct state_machine *sm)
{
    ibuf_free(sm->cgdb_gdbmi_buffer);
    sm->cgdb_gdbmi_buffer = NULL;

    ibuf_free(sm->tgdb_buffer);
    sm->tgdb_buffer = NULL;

    free(sm);
}

enum internal_state data_get_state(struct state_machine *d)
{
    return d->data_state;
}

void data_set_state(struct annotate_two *a2, enum internal_state state)
{
    /* if tgdb is at an internal command, than nothing changes that
     * state unless tgdb gets to the prompt annotation. This means that
     * the internal command is done */

    a2->sm->data_state = state;

    switch (a2->sm->data_state) {
        case VOID:
        case USER_COMMAND:
            break;
        case AT_PROMPT:
            a2->sm->gdb_prompt_size = 0;
            break;
        case USER_AT_PROMPT:
            /* Null-Terminate the prompt */
            a2->sm->gdb_prompt[a2->sm->gdb_prompt_size] = '\0';

            if (strcmp(a2->sm->gdb_prompt, a2->sm->gdb_prompt_last) != 0) {
                strcpy(a2->sm->gdb_prompt_last, a2->sm->gdb_prompt);
                /* Update the prompt */
                if (a2->cur_response_list) {
                    struct tgdb_response *response = (struct tgdb_response *)
                            cgdb_malloc(sizeof (struct tgdb_response));

                    response->header = TGDB_UPDATE_CONSOLE_PROMPT_VALUE;
                    response->result_id = -1;
                    response->request = NULL;
                    response->choice.update_console_prompt_value.prompt_value =
                            cgdb_strdup(a2->sm->gdb_prompt_last);
                    tgdb_list_append(a2->cur_response_list, response);
                }
            }

            a2->command_finished = 1;
            break;
        case POST_PROMPT:
            a2->sm->data_state = VOID;
            break;
    }
}

static void data_process(struct annotate_two *a2,
        char a, char *buf, int *n, struct tgdb_list *list)
{
    switch (a2->sm->data_state) {
        case VOID:
            buf[(*n)++] = a;
            break;
        case AT_PROMPT:
            a2->sm->gdb_prompt[a2->sm->gdb_prompt_size++] = a;
            break;
        case USER_AT_PROMPT:
        case USER_COMMAND:
        case POST_PROMPT:
            break;
    }
}

/* This turns true if tgdb gets a misc prompt. This is so that we do not
 * send commands to gdb at this point.
 */
int globals_is_misc_prompt(struct state_machine *sm)
{
    return sm->misc_prompt_command;
}

void globals_set_misc_prompt_command(struct state_machine *sm, unsigned short set)
{
    sm->misc_prompt_command = set;
}

static int
handle_frame_end(struct annotate_two *a2, const char *buf, size_t n,
        struct tgdb_list *list)
{
    /* set up the info_source command to get file info */
    return a2_get_current_location(a2, NULL);
}

static int
handle_breakpoints_invalid(struct annotate_two *a2, const char *buf, size_t n,
        struct tgdb_list *list)
{
    return commands_issue_command(a2->client_command_list,
                           ANNOTATE_INFO_BREAKPOINTS, NULL, 0, NULL);
}

static int handle_misc_pre_prompt(struct annotate_two *a2, const char *buf,
        size_t n, struct tgdb_list *list)
{
    /* If tgdb is sending a command, then continue past it */
    data_set_state(a2, AT_PROMPT);
    return 0;
}

static int handle_misc_prompt(struct annotate_two *a2, const char *buf,
        size_t n, struct tgdb_list *list)
{
    globals_set_misc_prompt_command(a2->sm, 1);
    data_set_state(a2, USER_AT_PROMPT);
    a2->command_finished = 1;
    return 0;
}

static int handle_misc_post_prompt(struct annotate_two *a2, const char *buf,
        size_t n, struct tgdb_list *list)
{
    globals_set_misc_prompt_command(a2->sm, 0);
    data_set_state(a2, POST_PROMPT);
    return 0;
}

static int handle_pre_prompt(struct annotate_two *a2, const char *buf, size_t n,
        struct tgdb_list *list)
{
    data_set_state(a2, AT_PROMPT);
    return 0;
}

static int handle_cgdb_gdbmi(struct annotate_two *a2, const char *buf, size_t n,
        struct tgdb_list *list)
{
    /* Return cgdb-gdbmi ID */
    return atoi(buf + strlen("cgdb-gdbmi"));
}

static int handle_prompt(struct annotate_two *a2, const char *buf, size_t n,
        struct tgdb_list *list)
{
    /* All done. */
    data_set_state(a2, USER_AT_PROMPT);
    return 0;
}

static int handle_post_prompt(struct annotate_two *a2, const char *buf,
        size_t n, struct tgdb_list *list)
{
    data_set_state(a2, POST_PROMPT);
    return 0;
}

static int handle_error(struct annotate_two *a2, const char *buf, size_t n,
        struct tgdb_list *list)
{
    data_set_state(a2, POST_PROMPT);    /* TEMPORARY */
    return 0;
}

static int handle_error_begin(struct annotate_two *a2, const char *buf,
        size_t n, struct tgdb_list *list)
{
    /* After a signal is sent (^c), the debugger will then output
     * something like "Quit\n", so that should be displayed to the user.
     * Unfortunately, the debugger ( gdb ) isn't nice enough to return a
     * post-prompt when a signal is received.
     */
    data_set_state(a2, VOID);
    return 0;
}

static int handle_quit(struct annotate_two *a2, const char *buf, size_t n,
        struct tgdb_list *list)
{
    data_set_state(a2, POST_PROMPT);    /* TEMPORARY */
    return 0;
}

static int handle_exited(struct annotate_two *a2, const char *buf, size_t n,
        struct tgdb_list *list)
{
    int exit_status;
    struct tgdb_response *response;

    // Buf should be something like:
    //    "exited 0"
    exit_status = (n >= 7) ? atoi(buf + 7) : -1;

    response = (struct tgdb_response *)cgdb_malloc(sizeof (struct tgdb_response));
    response->result_id = -1;
    response->request = NULL;
    response->header = TGDB_INFERIOR_EXITED;
    response->choice.inferior_exited.exit_status = exit_status;
    tgdb_types_append_command(list, response);
    return 0;
}

/**
 * The main annotation data structure.
 * It represents all of the supported annotataions that can be parsed.
 */
static struct annotation {

    /** The name of the annotation. */
    const char *data;

    /** The size of the annotation. */
    size_t size;

    /** The function to call when the annotation is found. */
    int (*f) (struct annotate_two * a2, const char *buf, size_t n,
            struct tgdb_list * list);
} annotations[] = {
    {
    "cgdb-gdbmi", 10, handle_cgdb_gdbmi}, {
    "breakpoints-invalid", 19, handle_breakpoints_invalid}, {
    "frame-end", 10, handle_frame_end}, {
    "pre-commands", 12, handle_misc_pre_prompt}, {
    "commands", 8, handle_misc_prompt}, {
    "post-commands", 13, handle_misc_post_prompt}, {
    "pre-overload-choice", 19, handle_misc_pre_prompt}, {
    "overload-choice", 15, handle_misc_prompt}, {
    "post-overload-choice", 20, handle_misc_post_prompt}, {
    "pre-instance-choice", 19, handle_misc_pre_prompt}, {
    "instance-choice", 15, handle_misc_prompt}, {
    "post-instance-choice", 20, handle_misc_post_prompt}, {
    "pre-query", 9, handle_misc_pre_prompt}, {
    "query", 5, handle_misc_prompt}, {
    "post-query", 10, handle_misc_post_prompt}, {
    "pre-prompt-for-continue", 23, handle_misc_pre_prompt}, {
    "prompt-for-continue", 19, handle_misc_prompt}, {
    "post-prompt-for-continue", 24, handle_misc_post_prompt}, {
    "pre-prompt", 10, handle_pre_prompt}, {
    "prompt", 6, handle_prompt}, {
    "post-prompt", 11, handle_post_prompt}, {
    "error-begin", 11, handle_error_begin}, {
    "error", 5, handle_error}, {
    "quit", 4, handle_quit}, {
    "exited", 6, handle_exited}, {
    NULL, 0, NULL}
};

static int tgdb_parse_annotation(struct annotate_two *a2, char *data, size_t size,
        struct tgdb_list *list, int *is_cgdb_gdbmi)
{
    int i;

    *is_cgdb_gdbmi = 0;

    for (i = 0; annotations[i].data != NULL; ++i) {
        if (strncmp(data, annotations[i].data, annotations[i].size) == 0) {
            *is_cgdb_gdbmi = (annotations[i].f == handle_cgdb_gdbmi);

            return annotations[i].f(a2, data, size, list);
        }
    }

    /* err_msg("ANNOTION(%s)", data); */
    return 0;
}

int a2_handle_data(struct annotate_two *a2, struct state_machine *sm,
        const char *data, const size_t size,
        char *gui_data, size_t * gui_size, struct tgdb_list *command_list)
{
    int i, counter = 0;

    /* track state to find next file and line number */
    for (i = 0; i < size; ++i) {
        /* Handle cgdb_gdbmi block */
        if (sm->tgdb_state == SM_CGDB_GDBMI) {
            if (data[i] != '\r')
                ibuf_addchar(sm->cgdb_gdbmi_buffer, data[i]);

            if (data[i] == '\n') {
                int id;
                char *result_line;
                int result_record = mi_get_result_record(sm->cgdb_gdbmi_buffer, &result_line, &id);

                if (result_record != -1) {
                    commands_process_cgdb_gdbmi(a2, sm->cgdb_gdbmi_buffer,
                            result_record, result_line, id, command_list);

                    sm->tgdb_state = SM_NL_DATA;
                    ibuf_clear(sm->cgdb_gdbmi_buffer);
                }
            }

            continue;
        }

        switch (data[i]) {
            /* Ignore all car returns outputted by gdb */
            case '\r':
                break;
            case '\n':
                switch (sm->tgdb_state) {
                case SM_DATA:
                    sm->tgdb_state = SM_NEW_LINE;
                    break;
                case SM_NEW_LINE:
                    sm->tgdb_state = SM_NEW_LINE;
                    data_process(a2, '\n', gui_data, &counter, command_list);
                    break;
                case SM_CONTROL_Z:
                    sm->tgdb_state = SM_DATA;
                    data_process(a2, '\n', gui_data, &counter, command_list);
                    data_process(a2, '\032', gui_data, &counter, command_list);
                    break;
                case SM_ANNOTATION: {
                    /* Found an annotation */
                    int is_cgdb_gdbmi;

                    sm->tgdb_state = SM_NL_DATA;
                    tgdb_parse_annotation(a2, ibuf_get(sm->tgdb_buffer),
                            ibuf_length(sm->tgdb_buffer), command_list,
                            &is_cgdb_gdbmi);

                    if (is_cgdb_gdbmi) {
                        ibuf_adddata(sm->cgdb_gdbmi_buffer,
                                     ibuf_get(sm->tgdb_buffer), ibuf_length(sm->tgdb_buffer));
                        sm->tgdb_state = SM_CGDB_GDBMI;
                    }

                    ibuf_clear(sm->tgdb_buffer);
                    break;
                    }
                case SM_NL_DATA:
                    sm->tgdb_state = SM_NEW_LINE;
                    break;
                default:
                    logger_write_pos(logger, __FILE__, __LINE__,
                            "Bad state transition");
                    break;
                }               /* end switch */
                break;

            case '\032':
                switch (sm->tgdb_state) {
                    case SM_DATA:
                        sm->tgdb_state = SM_DATA;
                        data_process(a2, '\032', gui_data, &counter,
                                command_list);
                        break;
                    case SM_NEW_LINE:
                        sm->tgdb_state = SM_CONTROL_Z;
                        break;
                    case SM_NL_DATA:
                        sm->tgdb_state = SM_CONTROL_Z;
                        break;
                    case SM_CONTROL_Z:
                        sm->tgdb_state = SM_ANNOTATION;
                        break;
                    case SM_ANNOTATION:
                        ibuf_addchar(sm->tgdb_buffer, data[i]);
                        break;
                    default:
                        logger_write_pos(logger, __FILE__, __LINE__,
                                "Bad state transition");
                        break;
                }               /* end switch */
                break;
            default:
                switch (sm->tgdb_state) {
                    case SM_DATA:
                        data_process(a2, data[i], gui_data, &counter, command_list);
                        break;
                    case SM_NL_DATA:
                        sm->tgdb_state = SM_DATA;
                        data_process(a2, data[i], gui_data, &counter, command_list);
                        break;
                    case SM_NEW_LINE:
                        sm->tgdb_state = SM_DATA;
                        data_process(a2, '\n', gui_data, &counter, command_list);
                        data_process(a2, data[i], gui_data, &counter, command_list);
                        break;
                    case SM_CONTROL_Z:
                        sm->tgdb_state = SM_DATA;
                        data_process(a2, '\n', gui_data, &counter, command_list);
                        data_process(a2, '\032', gui_data, &counter, command_list);
                        data_process(a2, data[i], gui_data, &counter, command_list);
                        break;
                    case SM_ANNOTATION: {
                        int end;
                        for (end = i + 1; end < size; end++) {
                            if ((data[end] == '\r') || (data[end] == '\n') || (data[end] == '\032')) {
                                break;
                            }
                        }

                        ibuf_adddata(sm->tgdb_buffer, data + i, end - i);
                        i = end - 1;
                        break;
                    }
                    default:
                        logger_write_pos(logger, __FILE__, __LINE__,
                                "Bad state transition");
                        break;
                }               /* end switch */
                break;
        }                       /* end switch */
    }                           /* end for */

    gui_data[counter] = '\0';
    *gui_size = counter;
    return 0;
}
