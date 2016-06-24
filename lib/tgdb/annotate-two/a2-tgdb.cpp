#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_STDIO_H
#include <stdio.h>
#endif /* HAVE_STDIO_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#if HAVE_SIGNAL_H
#include <signal.h>
#endif /* HAVE_SIGNAL_H */

#if HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */

#include "a2-tgdb.h"
#include "fork_util.h"
#include "fs_util.h"
#include "pseudo.h"
#include "logger.h"
#include "io.h"
#include "state_machine.h"
#include "data.h"
#include "commands.h"
#include "globals.h"
#include "tgdb_types.h"
#include "queue.h"
#include "sys_util.h"
#include "ibuf.h"
#include "annotate_two.h"

static int a2_set_inferior_tty(void *ctx)
{
    struct annotate_two *a2 = (struct annotate_two *) ctx;

    if (!a2) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "a2_set_inferior_tty error");
        return -1;
    }

    if (commands_issue_command(a2->c,
                    a2->client_command_list,
                    ANNOTATE_TTY,
                    pty_pair_get_slavename(a2->pty_pair), 0) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }

    return 0;
}

static int close_inferior_connection(void *ctx)
{
    struct annotate_two *a2 = (struct annotate_two *) ctx;

    if (!a2) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "close_inferior_connection error");
        return -1;
    }

    if (a2->pty_pair)
        pty_pair_destroy(a2->pty_pair);

    return 0;
}

/* Here are the two functions that deal with getting tty information out
 * of the annotate_two subsystem.
 */

int a2_open_new_tty(struct annotate_two *a2, int *inferior_stdin, int *inferior_stdout)
{
    close_inferior_connection(a2);

    a2->pty_pair = pty_pair_create();
    if (!a2->pty_pair) {
        logger_write_pos(logger, __FILE__, __LINE__, "pty_pair_create failed");
        return -1;
    }

    *inferior_stdin = pty_pair_get_masterfd(a2->pty_pair);
    *inferior_stdout = pty_pair_get_masterfd(a2->pty_pair);

    a2_set_inferior_tty(a2);

    return 0;
}

const char *a2_get_tty_name(struct annotate_two *a2)
{
    if (!a2) {
        logger_write_pos(logger, __FILE__, __LINE__, "a2_get_tty_name failed");
        return NULL;
    }

    return pty_pair_get_slavename(a2->pty_pair);
}

/* initialize_annotate_two
 *
 * initializes an annotate_two subsystem and sets up all initial values.
 */
static struct annotate_two *initialize_annotate_two(void)
{
    struct annotate_two *a2 = (struct annotate_two *)
            cgdb_malloc(sizeof (struct annotate_two));

    a2->tgdb_initialized = 0;
    a2->debugger_stdin = -1;
    a2->debugger_out = -1;

    a2->pty_pair = NULL;

    /* null terminate */
    a2->config_dir[0] = '\0';
    a2->a2_gdb_init_file[0] = '\0';

    a2->cur_response_list = NULL;

    return a2;
}

/* tgdb_setup_config_file: 
 * -----------------------
 *  Creates a config file for the user.
 *
 *  Pre: The directory already has read/write permissions. This should have
 *       been checked by tgdb-base.
 *
 *  Return: 1 on success or 0 on error
 */
static int tgdb_setup_config_file(struct annotate_two *a2, const char *dir)
{
    FILE *fp;

    strncpy(a2->config_dir, dir, strlen(dir) + 1);

    fs_util_get_path(dir, "a2_gdb_init", a2->a2_gdb_init_file);

    if ((fp = fopen(a2->a2_gdb_init_file, "w"))) {
        fprintf(fp, "set annotate 2\n" "set height 0\n");
        fclose(fp);
    } else {
        logger_write_pos(logger, __FILE__, __LINE__, "fopen error '%s'",
                a2->a2_gdb_init_file);
        return 0;
    }

    return 1;
}

struct annotate_two *a2_create_context(const char *debugger,
        int argc, char **argv, const char *config_dir, struct logger *logger_in)
{

    struct annotate_two *a2 = initialize_annotate_two();
    char a2_debug_file[FSUTIL_PATH_MAX];

    if (!tgdb_setup_config_file(a2, config_dir)) {
        logger_write_pos(logger_in, __FILE__, __LINE__,
                "tgdb_init_config_file error");
        return NULL;
    }

    /* Initialize the debug file that a2_tgdb writes to */
    fs_util_get_path(config_dir, "a2_tgdb_debug.txt", a2_debug_file);
    io_debug_init(a2_debug_file);

    a2->debugger_pid =
            invoke_debugger(debugger, argc, argv,
            &a2->debugger_stdin, &a2->debugger_out, 0, a2->a2_gdb_init_file);

    /* Couldn't invoke process */
    if (a2->debugger_pid == -1)
        return NULL;

    return a2;
}

int a2_initialize(struct annotate_two *a2,
        int *debugger_stdin, int *debugger_stdout,
        int *inferior_stdin, int *inferior_stdout)
{
    *debugger_stdin = a2->debugger_stdin;
    *debugger_stdout = a2->debugger_out;

    a2->data = data_initialize();
    a2->sm = state_machine_initialize();
    a2->c = commands_initialize();
    a2->g = globals_initialize();
    a2->client_command_list = tgdb_list_init();

    a2_open_new_tty(a2, inferior_stdin, inferior_stdout);

    /* Need to get source information before breakpoint information otherwise
     * the TGDB_UPDATE_BREAKPOINTS event will be ignored in process_commands()
     * because there are no source files to add the breakpoints to.
     */

    a2_get_current_location(a2);

    /* gdb may already have some breakpoints when it starts. This could happen
     * if the user puts breakpoints in there .gdbinit.
     * This makes sure that TGDB asks for the breakpoints on start up.
     */
    if (commands_issue_command(a2->c,
                    a2->client_command_list,
                    ANNOTATE_INFO_BREAKPOINTS, NULL, 0) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }

    a2->tgdb_initialized = 1;

    return 0;
}

/* tgdb_list_free expects a "int (*)(void)" function, and
 * queue_free_list wants a "void (*)(void). This function is here
 * to wrap tgdb_command_destroy for tgdb_list_free.
 */
static int tgdb_command_destroy_list_item(void *item)
{
    tgdb_command_destroy(item);
    return 0;
}

/* TODO: Implement shutdown properly */
int a2_shutdown(struct annotate_two *a2)
{
    cgdb_close(a2->debugger_stdin);

    data_shutdown(a2->data);
    state_machine_shutdown(a2->sm);
    commands_shutdown(a2->c);

    tgdb_list_free(a2->client_command_list, tgdb_command_destroy_list_item);
    tgdb_list_destroy(a2->client_command_list);

    tgdb_list_free(a2->cur_response_list, tgdb_types_free_command);
    tgdb_list_destroy(a2->cur_response_list);

    globals_shutdown(a2->g);
    return 0;
}

/* TODO: Implement error messages. */
int a2_err_msg(struct annotate_two *a2)
{
    return -1;
}

int a2_is_client_ready(struct annotate_two *a2)
{
    if (!a2->tgdb_initialized)
        return 0;

    /* If the user is at the prompt */
    if (data_get_state(a2->data) == USER_AT_PROMPT)
        return 1;

    return 0;
}

int a2_parse_io(struct annotate_two *a2,
        const char *input_data, const size_t input_data_size,
        char *debugger_output, size_t * debugger_output_size,
        char *inferior_output, size_t * inferior_output_size,
        struct tgdb_list *list)
{
    int val;

    a2->command_finished = 0;
    a2->cur_response_list = list;

    val = a2_handle_data(a2, a2->sm, input_data, input_data_size,
            debugger_output, debugger_output_size, list);

    a2->cur_response_list = NULL;

    if (a2->command_finished)
        return 1;
    else
        return 0;
}

struct tgdb_list *a2_get_client_commands(struct annotate_two *a2)
{
    return a2->client_command_list;
}

int a2_get_current_location(struct annotate_two *a2)
{
    int ret;

    /* set up the info_source command to get info */
    ret = commands_issue_command(a2->c, a2->client_command_list,
                    ANNOTATE_INFO_SOURCE, NULL, 1);
    if (ret == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }

    return 0;
}

int a2_get_inferior_sources(struct annotate_two *a2)
{
    if (commands_issue_command(a2->c,
                    a2->client_command_list,
                    ANNOTATE_INFO_SOURCES, NULL, 0) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }

    return 0;
}

const char *a2_return_client_command(struct annotate_two *a2, enum tgdb_command_type c)
{
    const char *ret = NULL;

    switch (c) {
        case TGDB_CONTINUE:
            ret = "continue";
            break;
        case TGDB_FINISH:
            ret = "finish";
            break;
        case TGDB_NEXT:
            ret = "next";
            break;
        case TGDB_START:
            ret = "start";
            break;
        case TGDB_RUN:
            ret = "run";
            break;
        case TGDB_KILL:
            ret = "kill";
            break;
        case TGDB_STEP:
            ret = "step";
            break;
        case TGDB_UNTIL:
            ret = "until";
            break;
        case TGDB_DISASSEMBLE:
            ret = "disassemble";
            break;
        case TGDB_UP:
            ret = "up";
            break;
        case TGDB_DOWN:
            ret = "down";
            break;
        case TGDB_ERROR:
            break;
    }

    return ret;
}

char *a2_client_modify_breakpoint(struct annotate_two *a2,
        const char *file, int line, enum tgdb_breakpoint_action b)
{
    char *val = (char *) cgdb_malloc(sizeof (char) * (strlen(file) + 128));

    if (b == TGDB_BREAKPOINT_ADD) {
        sprintf(val, "break \"%s\":%d", file, line);
        return val;
    } else if (b == TGDB_BREAKPOINT_DELETE) {
        sprintf(val, "clear \"%s\":%d", file, line);
        return val;
    } else if (b == TGDB_TBREAKPOINT_ADD) {
        sprintf(val, "tbreak \"%s\":%d", file, line);
        return val;
    } else
        return NULL;
}

pid_t a2_get_debugger_pid(struct annotate_two *a2)
{
    return a2->debugger_pid;
}

int a2_completion_callback(struct annotate_two *a2, const char *command)
{
    if (commands_issue_command(a2->c,
                    a2->client_command_list,
                    ANNOTATE_COMPLETE, command, 4) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }

    return 0;
}

int a2_user_ran_command(struct annotate_two *a2)
{
    return commands_user_ran_command(a2->c, a2->client_command_list);
}

int a2_prepare_for_command(struct annotate_two *a2, struct tgdb_command *com)
{
    return commands_prepare_for_command(a2, a2->c, com);
}

int a2_is_misc_prompt(struct annotate_two *a2)
{
    return globals_is_misc_prompt(a2->g);
}
