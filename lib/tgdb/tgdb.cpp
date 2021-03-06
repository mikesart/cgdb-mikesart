/* Includes {{{ */
#if HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_STDIO_H
#include <stdio.h>
#endif /* HAVE_STDIO_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if HAVE_SIGNAL_H
#include <signal.h> /* sig_atomic_t */
#endif

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <inttypes.h>

#include "a2-tgdb.h"
#include "tgdb.h"
#include "commands.h"
#include "state_machine.h"
#include "ibuf.h"
#include "io.h"
#include "pseudo.h" /* SLAVE_SIZE constant */

/* }}} */

static int last_request_requires_update = -1;
static tgdb_request_ptr *requests_with_ids = NULL;

/* struct tgdb {{{ */

/**
 * The TGDB context data structure.
 */
struct tgdb
{
    /** A client context to abstract the debugger.  */
    struct annotate_two *a2;

    /** Reading from this will read from the debugger's output */
    int debugger_stdout;

    /** Writing to this will write to the debugger's stdin */
    int debugger_stdin;

    /** Reading from this will read the stdout from the program being debugged */
    int inferior_stdout;

    /** Writing to this will write to the stdin of the program being debugged */
    int inferior_stdin;

    /***************************************************************************
     * All the queue's the clients can run commands through
     * The different queue's can be slightly confusing.
     **************************************************************************/

    /**
     * The commands that need to be run through GDB.
     *
     * This is a buffered queue that represents all of the commands that TGDB
     * needs to execute. These commands will be executed one at a time. That is,
     * 1 command will be issued, and TGDB will wait for the entire response before
     * issuing any more commands. It is even possible that while this command is
     * executing, that the client context will add commands to the
     * oob_command_queue. If this happens TGDB will execute all of the commands
     * in the oob_command_queue before executing the next command in this queue.
     */
    struct tgdb_command **gdb_input_queue;

    /** 
     * The commands that the client has requested to run.
     *
     * TGDB buffers all of the commands that the FE wants to run. That is,
     * if the FE is sending commands faster than TGDB can pass the command to
     * GDB and get a response, then the commands are buffered until it is their
     * turn to run. These commands are run in the order that they are received.
     *
     * This is here as a convenience to the FE. TGDB currently does not access
     * these commands. It provides the push/pop functionality and it erases the
     * queue when a control_c is received.
     */
    tgdb_request_ptr *gdb_client_request_queue;

    /** 
     * The out of band input queue.
     *
     * These commands are used by the client context. When a single client context
     * command is run, sometimes it will discover that it needs to run other commands
     * in order to satisfy the functionality requested by the GUI. For instance, a
     * single GUI function request could take N client context commands to generate
     * a response. Also, to make things worse, sometimes the client context doesn't
     * know if it will take 1 or N commands to satisfy the request until after it has
     * sent and received the information from the first command. Thus, while the
     * client context is processing the first command, it may add more commands to
     * this queue to specify that these commands need to be run before any other
     * commands are sent to GDB.
     *
     * These commands should *always* be run first.
     */
    struct tgdb_command **oob_input_queue;

    /** These are 2 very important state variables.  */

    /**
     * If set to 1, libtgdb thinks the lower level subsystem is capable of
     * receiving another command. It needs this so that it doesn't send 2
     * commands to the lower level before it can say it can't receive a command.
     * At some point, maybe this can be removed?
     * When its set to 0, libtgdb thinks it can not send the lower level another
     * command.
     *
     * Basically whether gdb is at prompt or not. */
    int is_gdb_ready_for_next_command;

    /** If ^c was hit by user */
    sig_atomic_t control_c;

    /**
     * This is the last GUI command that has been run.
     * It is used to display to the client the GUI commands.
     *
     * It will either be NULL when it is not set or it should be
     * the last GUI command run. If it is non-NULL it should be from the heap.
     * As anyone is allowed to call free on it.  */
    char *last_gui_command;

    /**
     * This is a TGDB option.
     * It determines if the user wants to see the commands the GUI is running.
     *
     * If it is 0, the user does not want to see the commands the GUI is
     * running. Otherwise, if it is 1, it does.  */
    int show_gui_commands;

    /**
     * When GDB dies (purposely or not), the SIGCHLD is sent to the application controlling TGDB.
     * This data structure represents the fact that SIGCHLD has been sent.
     *
     * This currently does not need to track if more than 1 SIGCHLD has been received. So
     * no matter how many are receieved, this will only be 1. Otherwise if none have been
     * received this will be 0.  */
    int has_sigchld_recv;
};

/* }}} */

/* Temporary prototypes {{{ */
static void tgdb_deliver_command(struct tgdb *tgdb, struct tgdb_command *command);
static void tgdb_unqueue_and_deliver_command(struct tgdb *tgdb);
static void tgdb_run_or_queue_command(struct tgdb *tgdb, struct tgdb_command *com);

/* }}} */

/* These functions are used to determine the state of libtgdb */

/**
 * Determines if tgdb should send data to gdb or put it in a buffer. This
 * is when the debugger is ready and there are no commands to run.
 *
 * \return
 * 1 if can issue directly to gdb. Otherwise 0.
 */
static int tgdb_can_issue_command(struct tgdb *tgdb)
{
    if (tgdb->is_gdb_ready_for_next_command &&
        a2_is_client_ready(tgdb->a2) &&
        (sbcount(tgdb->gdb_input_queue) == 0))
        return 1;

    return 0;
}

/**
 * Determines if tgdb has commands it needs to run.
 *
 * \return
 * 1 if can issue directly to gdb. Otherwise 0.
 */
static int tgdb_has_command_to_run(struct tgdb *tgdb)
{
    if (a2_is_client_ready(tgdb->a2) &&
        ((sbcount(tgdb->gdb_input_queue) > 0) ||
            (sbcount(tgdb->oob_input_queue) > 0)))
        return 1;

    return 0;
}

/**
 * If the TGDB instance is not busy, it will run the requested command.
 * Otherwise, the command will get queued to run later.
 *
 * \param tgdb_in
 * An instance of the tgdb library to operate on.
 *
 * \param request
 * The requested command to have TGDB process.
 *
 * \return
 * 0 on success or -1 on error
 */
static void handle_request(struct tgdb *tgdb_in, struct tgdb_request *request)
{
    if (tgdb_can_issue_command(tgdb_in))
    {
        tgdb_process_command(tgdb_in, request);
    }
    else
    {
        tgdb_queue_append(tgdb_in, request);
    }
}

tgdb_request_ptr tgdb_pop_request_id(int id)
{
    if (id > 0)
    {
        int i;
        int count = sbcount(requests_with_ids);

        for (i = 0; i < count; i++)
        {
            if (requests_with_ids[i]->id == id)
            {
                tgdb_request_ptr ptr = requests_with_ids[i];

                requests_with_ids[i] = requests_with_ids[count - 1];
                sbsetcount(requests_with_ids, count - 1);
                return ptr;
            }
        }
    }

    return NULL;
}

void tgdb_set_last_request(struct tgdb_request *request)
{
    if (request)
    {
        switch (request->header)
        {
        case TGDB_REQUEST_CONSOLE_COMMAND:
        case TGDB_REQUEST_DEBUGGER_COMMAND:
        case TGDB_REQUEST_MODIFY_BREAKPOINT:
        case TGDB_REQUEST_COMPLETE:
            last_request_requires_update = 1;
            break;
        case TGDB_REQUEST_INFO_SOURCES:
        case TGDB_REQUEST_DISASSEMBLE:
        case TGDB_REQUEST_DISASSEMBLE_FUNC:
        case TGDB_REQUEST_BREAKPOINTS:
        case TGDB_REQUEST_FRAME:
        default:
            last_request_requires_update = 0;
            break;
        }

        if (request->id > 0)
            sbpush(requests_with_ids, request);
        else
            tgdb_request_destroy(request);
    }
    else
    {
        last_request_requires_update = 0;
    }
}

/**
 * This function looks at the request that CGDB has made and determines if it
 * effects the GDB console window. For instance, if the request makes output go
 * to that window, then the user would like to see another prompt there when the
 * command finishes. However, if the command is 'o', to get all the sources and
 * display them, then this doesn't effect the GDB console window and the window
 * should not redisplay the prompt.
 *
 * \param request
 * The request to analysize
 *
 * \param update
 * Will return as 1 if the console window should be updated, or 0 otherwise
 *
 * \return
 * 0 on success or -1 on error
 */
int tgdb_last_request_requires_update()
{
    return last_request_requires_update;
}

/**
 * Process the commands that were created by the client
 *
 * \param tgdb
 * The TGDB context
 *
 * \return
 * -1 on error, 0 on success
 */
static void tgdb_process_client_commands(struct tgdb *tgdb)
{
    int i;

    for (i = 0; i < sbcount(tgdb->a2->client_commands); i++)
    {
        struct tgdb_command *command = tgdb->a2->client_commands[i];

        tgdb_run_or_queue_command(tgdb, command);
    }

    /* Clear the client command array */
    sbsetcount(tgdb->a2->client_commands, 0);
}

static struct tgdb *initialize_tgdb_context(void)
{
    struct tgdb *tgdb = (struct tgdb *)cgdb_malloc(sizeof(struct tgdb));

    tgdb->a2 = NULL;
    tgdb->control_c = 0;

    tgdb->debugger_stdout = -1;
    tgdb->debugger_stdin = -1;

    tgdb->inferior_stdout = -1;
    tgdb->inferior_stdin = -1;

    tgdb->gdb_client_request_queue = NULL;
    tgdb->gdb_input_queue = NULL;
    tgdb->oob_input_queue = NULL;

    tgdb->is_gdb_ready_for_next_command = 1;

    tgdb->last_gui_command = NULL;
    tgdb->show_gui_commands = 0;

    tgdb->has_sigchld_recv = 0;

    return tgdb;
}

/*******************************************************************************
 * This is the basic initialization
 ******************************************************************************/

/* 
 * Gets the users home dir and creates the config directory.
 *
 * \param tgdb
 * The tgdb context.
 *
 * \param config_dir 
 * Should be FSUTIL_PATH_MAX in size on way in.
 * On way out, it will be the path to the config dir
 *
 * \return
 * -1 on error, or 0 on success
 */
static int tgdb_initialize_config_dir(struct tgdb *tgdb, char *config_dir)
{
    char cgdb_dir[FSUTIL_PATH_MAX];
    char *home_dir = getenv("HOME");

    /* Make sure the toplevel .cgdb dir exists */
    snprintf(cgdb_dir, sizeof(cgdb_dir), "%s/.cgdb", home_dir);
    fs_util_create_dir(cgdb_dir);

    /* Try to create full .cgdb/logs directory */
    snprintf(config_dir, FSUTIL_PATH_MAX, "%s/.cgdb/logs", home_dir);
    if (!fs_util_create_dir(config_dir))
    {
        clog_error(CLOG_CGDB, "fs_util_create_dir_in_base error");
        return -1;
    }

    return 0;
}

static int clog_open(int id, const char *fmt, const char *config_dir)
{
    int i;

    /* Try to open a log file exclusively. This allows us to run
     * several instances of cgdb without the logfiles getting borked. */
    for (i = 1; i < 100; i++)
    {
        char filename[FSUTIL_PATH_MAX];

        /* Initialize the debug file that a2_tgdb writes to */
        snprintf(filename, sizeof(filename), fmt, config_dir, i);

        if (clog_init_path(id, filename) == 0)
            return 0;
    }

    return -1;
}

/**
 * Knowing the user's home directory, TGDB can initialize the logger interface
 *
 * \param tgdb
 * The tgdb context.
 *
 * \param config_dir 
 * The path to the user's config directory
 *
 * \return
 * -1 on error, or 0 on success
 */
static int tgdb_initialize_logger_interface(struct tgdb *tgdb, char *config_dir)
{
    /* Open our cgdb and tgdb io logfiles */
    clog_open(CLOG_CGDB_ID, "%s/cgdb_log%d.txt", config_dir);
    clog_open(CLOG_GDBIO_ID, "%s/cgdb_gdb_io_log%d.txt", config_dir);

    /* Puts cgdb in a mode where it writes a debug log of everything
     *    that is read from gdb. That is basically the entire session. This info
     *    is useful in determining what is going on under tgdb since the gui
     *    is good at hiding that info from the user.
     *
     * Change level to CLOG_ERROR to write only error messages.
     */
    clog_set_level(CLOG_GDBIO_ID, CLOG_DEBUG);

    /* General cgdb logging. Only logging warnings and debug messages
       by default. */
    clog_set_level(CLOG_CGDB_ID, CLOG_WARN);

    return 0;
}

/* Creating and Destroying a libtgdb context. {{{*/

struct tgdb *tgdb_initialize(const char *debugger,
    int argc, char **argv, int *debugger_fd)
{
    /* Initialize the libtgdb context */
    struct tgdb *tgdb = initialize_tgdb_context();
    char config_dir[FSUTIL_PATH_MAX];

    /* Create config directory */
    if (tgdb_initialize_config_dir(tgdb, config_dir) == -1)
    {
        clog_error(CLOG_CGDB, "tgdb_initialize error");
        return NULL;
    }

    if (tgdb_initialize_logger_interface(tgdb, config_dir) == -1)
    {
        printf("Could not initialize logger interface\n");
        return NULL;
    }

    tgdb->a2 = a2_create_context(debugger, argc, argv, config_dir);

    /* create an instance and initialize a tgdb_client_context */
    if (tgdb->a2 == NULL)
    {
        clog_error(CLOG_CGDB, "a2_create_context failed");
        return NULL;
    }

    if (a2_initialize(tgdb->a2,
            &(tgdb->debugger_stdin),
            &(tgdb->debugger_stdout),
            &(tgdb->inferior_stdin), &(tgdb->inferior_stdout)) == -1)
    {
        clog_error(CLOG_CGDB, "tgdb_client_initialize failed");
        return NULL;
    }

    tgdb_process_client_commands(tgdb);

    *debugger_fd = tgdb->debugger_stdout;

    return tgdb;
}

int tgdb_shutdown(struct tgdb *tgdb)
{
    return a2_shutdown(tgdb->a2);
}

void tgdb_close_logfiles()
{
    clog_info(CLOG_CGDB, "Closing logfile.");
    clog_free(CLOG_CGDB_ID);

    clog_info(CLOG_GDBIO, "Closing logfile.");
    clog_free(CLOG_GDBIO_ID);
}

/* }}}*/

static const char *tgdb_get_client_command(struct tgdb *tgdb,
    enum tgdb_command_type c)
{
    switch (c)
    {
    case TGDB_CONTINUE:
        return "continue";
    case TGDB_FINISH:
        return "finish";
    case TGDB_NEXT:
        return "next";
    case TGDB_START:
        return "start";
    case TGDB_RUN:
        return "run";
    case TGDB_KILL:
        return "kill";
    case TGDB_STEP:
        return "step";
    case TGDB_UNTIL:
        return "until";
    case TGDB_UP:
        return "up";
    case TGDB_DOWN:
        return "down";
    }

    return NULL;
}

static char *tgdb_client_modify_breakpoint_call(struct tgdb *tgdb,
    const char *file, int line, uint64_t addr, enum tgdb_breakpoint_action b)
{
    const char *action;

    switch (b)
    {
    default:
    case TGDB_BREAKPOINT_ADD:
        action = "break";
        break;
    case TGDB_BREAKPOINT_DELETE:
        action ="clear";
        break;
    case TGDB_TBREAKPOINT_ADD:
        action = "tbreak";
        break;
    }

    if (file)
        return sys_aprintf("%s \"%s\":%d", action, file, line);

    return sys_aprintf("%s *0x%" PRIx64, action, addr);
}

static void tgdb_disassemble(struct annotate_two *a2, uint64_t addr, int lines, int *id)
{
    char *data;

    if (!lines)
        lines = 100;

    if (addr)
        data = sys_aprintf("%di 0x%" PRIx64, lines, addr);
    else
        data = sys_aprintf("%di $pc", lines);

    commands_issue_command(a2, ANNOTATE_DISASSEMBLE, data, 0, id);

    free(data);
}

static void tgdb_disassemble_func(struct annotate_two *a2, int raw, int source,
    const char *file, const char *function, int *id)
{
    /* GDB 7.11 adds /s command to disassemble

    https://sourceware.org/git/gitweb.cgi?p=binutils-gdb.git;a=commit;h=6ff0ba5f7b8a2b10642bbb233a32043595c55670
        The "source centric" /m option to the disassemble command is often
        unhelpful, e.g., in the presence of optimized code.
        This patch adds a /s modifier that is better.
        For one, /m only prints instructions from the originating source file,
        leaving out instructions from e.g., inlined functions from other files.

    disassemble
         /m: source lines included
         /s: source lines included, output in pc order (7.10 and higher)
         /r: raw instructions included in hex
         single argument: function surrounding is dumped
         two arguments: start,end or start,+length
         disassemble 'driver.cpp'::main
         interp mi "disassemble /s 'driver.cpp'::main,+10"
         interp mi "disassemble /r 'driver.cpp'::main,+10"
     */
    char *data = NULL;
    int gdb_version_major;
    int gdb_version_minor;
    const char *source_line_flag = "/m ";

    tgdb_get_gdb_version(&gdb_version_major, &gdb_version_minor);

    /* GDB versions 7.11 and above support /s with disassemble. */
    if ((gdb_version_major > 7) ||
        (gdb_version_major == 7 && gdb_version_minor >= 11))
    {
        source_line_flag = "/s ";
    }

    if (raw || source || function)
    {
        const char *raw_flag = raw ? "/r " : " ";
        const char *source_flag = source ? source_line_flag : " ";

        if (file)
            data = sys_aprintf("%s%s'%s'::%s", raw_flag, source_flag, file, function);
        else
            data = sys_aprintf("%s%s%s", raw_flag, source_flag, function ? function : "");
    }

    commands_issue_command(a2, ANNOTATE_DISASSEMBLE_FUNC, data, 0, id);

    free(data);
}

void tgdb_request_destroy(tgdb_request_ptr request_ptr)
{
    if (!request_ptr)
        return;

    switch (request_ptr->header)
    {
    case TGDB_REQUEST_CONSOLE_COMMAND:
        free((char *)request_ptr->choice.console_command.command);
        request_ptr->choice.console_command.command = NULL;
        break;
    case TGDB_REQUEST_MODIFY_BREAKPOINT:
        free((char *)request_ptr->choice.modify_breakpoint.file);
        request_ptr->choice.modify_breakpoint.file = NULL;
        break;
    case TGDB_REQUEST_COMPLETE:
        free((char *)request_ptr->choice.complete.line);
        request_ptr->choice.complete.line = NULL;
        break;
    case TGDB_REQUEST_DISASSEMBLE_FUNC:
        if (request_ptr->choice.disassemble_func.tfp)
        {
            struct tgdb_file_position *tfp =
                request_ptr->choice.disassemble_func.tfp;

            free(tfp->absolute_path);
            free(tfp->from);
            free(tfp->func);
            free(tfp);

            request_ptr->choice.disassemble_func.tfp = NULL;
        }
        break;
    case TGDB_REQUEST_DISASSEMBLE:
        if (request_ptr->choice.disassemble.tfp)
        {
            struct tgdb_file_position *tfp =
                request_ptr->choice.disassemble.tfp;

            free(tfp->absolute_path);
            free(tfp->from);
            free(tfp->func);
            free(tfp);

            request_ptr->choice.disassemble.tfp = NULL;
        }
        break;
    case TGDB_REQUEST_BREAKPOINTS:
    case TGDB_REQUEST_FRAME:
    case TGDB_REQUEST_INFO_SOURCES:
    case TGDB_REQUEST_DEBUGGER_COMMAND:
    default:
        break;
    }

    free(request_ptr);
    request_ptr = NULL;
}

static void tgdb_request_destroy_func(void *item)
{
    tgdb_request_destroy((tgdb_request_ptr)item);
}

/* tgdb_handle_signals
 */
static int tgdb_handle_signals(struct tgdb *tgdb)
{
    if (tgdb->control_c)
    {
        sbsetcount(tgdb->gdb_input_queue, 0);
        sbsetcount(tgdb->gdb_client_request_queue, 0);

        tgdb->control_c = 0;
    }

    return 0;
}

/*******************************************************************************
 * This is the main_loop stuff for tgdb-base
 ******************************************************************************/

/* 
 * Sends a command to the debugger. This function gets called when the GUI
 * wants to run a command.
 *
 * \param tgdb
 * An instance of the tgdb library to operate on.
 *
 * \param command
 * This is the command that should be sent to the debugger.
 *
 * \param command_choice
 * This tells tgdb_send who is sending the command.
 */
static void
tgdb_send(struct tgdb *tgdb, const char *command,
    enum tgdb_command_choice command_choice)
{
    struct tgdb_command *tc;
    char *command_data = NULL;
    int length = strlen(command);

    /* Add a newline to the end of the command if it doesn't exist */
    if (!length || (command[length - 1] != '\n'))
        command_data = sys_aprintf("%s\n", command);
    else
        command_data = strdup(command);

    /* Create the client command */
    tc = (struct tgdb_command *)cgdb_malloc(sizeof(struct tgdb_command));
    tc->command_choice = command_choice;
    tc->command = ANNOTATE_USER_COMMAND;
    tc->gdb_command = command_data;

    tgdb_run_or_queue_command(tgdb, tc);
    tgdb_process_client_commands(tgdb);
}

/**
 * If TGDB is ready to process another command, then this command will be
 * sent to the debugger. However, if TGDB is not ready to process another command,
 * then the command will be queued and run when TGDB is ready.
 *
 * \param tgdb
 * The TGDB context to use.
 *
 * \param command
 * The command to run or queue.
 *
 * \return
 * 0 on success or -1 on error
 */
static void
tgdb_run_or_queue_command(struct tgdb *tgdb, struct tgdb_command *command)
{
    int can_issue = tgdb_can_issue_command(tgdb);

    if (can_issue)
    {
        tgdb_deliver_command(tgdb, command);
        tgdb_command_destroy(command);
    }
    else
    {
        /* Make sure to put the command into the correct queue. */
        switch (command->command_choice)
        {
        case TGDB_COMMAND_FRONT_END:
        case TGDB_COMMAND_TGDB_CLIENT:
            sbpush(tgdb->gdb_input_queue, command);
            break;
        case TGDB_COMMAND_TGDB_CLIENT_PRIORITY:
            sbpush(tgdb->oob_input_queue, command);
            break;
        case TGDB_COMMAND_CONSOLE:
        default:
            clog_error(CLOG_CGDB, "unimplemented command");
            break;
        }
    }
}

/**
 * Will send a command to the debugger immediately. No queueing will be done
 * at this point.
 *
 * \param tgdb
 * The TGDB context to use.
 *
 * \param command 
 * The command to run.
 *
 *  NOTE: This function assummes valid commands are being sent to it. 
 *        Error checking should be done before inserting into queue.
 */
static void tgdb_deliver_command(struct tgdb *tgdb, struct tgdb_command *command)
{
    tgdb->is_gdb_ready_for_next_command = 0;

    /* Send what we're doing to log file */
    if (clog_get_level(CLOG_GDBIO_ID) <= CLOG_DEBUG)
    {
        char *str = sys_quote_nonprintables(command->gdb_command, -1);

        clog_debug(CLOG_GDBIO, "<%s>", str);
        sbfree(str);
    }

    /* Here is where the command is actually given to the debugger.
     * Before this is done, if the command is a GUI command, we save it,
     * so that later, it can be printed to the client. Its for debugging
     * purposes only, or for people who want to know the commands their
     * debugger is being given.
     */
    if (command->command_choice == TGDB_COMMAND_FRONT_END)
        tgdb->last_gui_command = cgdb_strdup(command->gdb_command);

    /* Set USER_COMMAND data state for user commands */
    if (command->command == ANNOTATE_USER_COMMAND)
        data_set_state(tgdb->a2, USER_COMMAND);

    /* Send command to gdb */
    io_writen(tgdb->debugger_stdin, command->gdb_command,
        strlen(command->gdb_command));

    /* Uncomment this if you wish to see all of the commands, that are
     * passed to GDB. */
    if (clog_get_level(CLOG_CGDB_ID) <= CLOG_INFO)
    {
        char *str = sys_quote_nonprintables(command->gdb_command, -1);

        clog_info(CLOG_CGDB, "<%s>", str);
        sbfree(str);
    }
}

/**
 * TGDB will search it's command queue's and determine what the next command
 * to deliever to GDB should be.
 *
 * \return
 * 0 on success, -1 on error
 */
static void tgdb_unqueue_and_deliver_command(struct tgdb *tgdb)
{
tgdb_unqueue_and_deliver_command_tag:

    /* This will redisplay the prompt when a command is run
     * through the gui with data on the console.
     */

    /* The out of band commands should always be run first */
    if (sbcount(tgdb->oob_input_queue) > 0)
    {
        /* These commands are always run. 
         * However, if an assumption is made that a misc
         * prompt can never be set while in this spot.
         */
        struct tgdb_command *item;

        item = sbpopfront(tgdb->oob_input_queue);
        tgdb_deliver_command(tgdb, item);
        tgdb_command_destroy(item);
    }
    /* If the queue is not empty, run a command */
    else if (sbcount(tgdb->gdb_input_queue) > 0)
    {
        struct tgdb_command *item;

        item = sbpopfront(tgdb->gdb_input_queue);

        /* If at the misc prompt, don't run the internal tgdb commands,
         * In fact throw them out for now, since they are only 
         * 'info breakpoints' */
        if (a2_is_misc_prompt(tgdb->a2) == 1)
        {
            if (item->command_choice != TGDB_COMMAND_CONSOLE)
            {
                tgdb_command_destroy(item);
                goto tgdb_unqueue_and_deliver_command_tag;
            }
        }

        tgdb_deliver_command(tgdb, item);
        tgdb_command_destroy(item);
    }
}

/* These functions are used to communicate with the inferior */
int tgdb_send_inferior_char(struct tgdb *tgdb, char c)
{
    if (io_write_byte(tgdb->inferior_stdout, c) == -1)
    {
        clog_error(CLOG_CGDB, "io_write_byte failed");
        return -1;
    }

    return 0;
}

/* returns to the caller data from the child */
/**
 * Returns output that the debugged program printed (the inferior).
 *
 * @param tgdb
 * The tgdb instance to act on.
 *
 * @param buf
 * The buffer to write the inferior data to.
 *
 * @param n
 * The number of bytes that buf can contain.
 *
 * @return
 * 0 on EOR, -1 on error, or the number of bytes written to buf.
 */
ssize_t tgdb_recv_inferior_data(struct tgdb *tgdb, char *buf, size_t n)
{
    char local_buf[n + 1];
    ssize_t size;

    /* read all the data possible from the child that is ready. */
    if ((size = io_read(tgdb->inferior_stdin, local_buf, n)) < 0)
    {
        clog_error(CLOG_CGDB, "inferior_fd read failed");
        return -1;
    }

    strncpy(buf, local_buf, size);
    buf[size] = '\0';

    return size;
}

/**
 * TGDB is going to quit.
 *
 * \param tgdb
 * The tgdb context
 *
 * \return
 * 0 on success or -1 on error
 */
static int tgdb_add_quit_command(struct tgdb *tgdb)
{
    struct tgdb_response *response;

    /* Child did not exit normally */
    response = tgdb_create_response(tgdb->a2, TGDB_QUIT);
    response->choice.quit.exit_status = -1;
    response->choice.quit.return_value = 0;
    return 0;
}

/**
 * This is called when GDB has finished.
 * Its job is to add the type of QUIT command that is appropriate.
 *
 * \param tgdb
 * The tgdb context
 *
 * \param tgdb_will_quit
 * This will return as 1 if tgdb sent the TGDB_QUIT command. Otherwise 0.
 * 
 * \return
 * 0 on success or -1 on error
 */
static int tgdb_get_quit_command(struct tgdb *tgdb, int *tgdb_will_quit)
{
    pid_t ret;
    int status = 0;
    struct tgdb_response *response;
    pid_t pid = a2_get_debugger_pid(tgdb->a2);

    if (!tgdb_will_quit)
        return -1;

    *tgdb_will_quit = 0;

    ret = waitpid(pid, &status, WNOHANG);

    if (ret == -1)
    {
        clog_error(CLOG_CGDB, "waitpid error");
        return -1;
    }
    else if (ret == 0)
    {
        /* This SIGCHLD wasn't for GDB */
        return 0;
    }

    response = tgdb_create_response(tgdb->a2, TGDB_QUIT);
    if ((WIFEXITED(status)) == 0)
    {
        /* Child did not exit normally */
        response->choice.quit.exit_status = -1;
        response->choice.quit.return_value = 0;
    }
    else
    {
        response->choice.quit.exit_status = 0;
        response->choice.quit.return_value = WEXITSTATUS(status);
    }

    *tgdb_will_quit = 1;
    return 0;
}

size_t tgdb_process(struct tgdb *tgdb, char *buf, size_t n, int *is_finished)
{
    char local_buf[10 * n];
    ssize_t size;
    size_t buf_size = 0;

    /* make the queue empty */
    a2_delete_responses(tgdb->a2);

    /* TODO: This is kind of a hack.
     * Since I know that I didn't do a read yet, the next select loop will
     * get me back here. This probably shouldn't return, however, I have to
     * re-write a lot of this function. Also, I think this function should
     * return a malloc'd string, not a static buffer.
     *
     * Currently, I see it as a bigger hack to try to just append this to the
     * beginning of buf.
     */
    if (tgdb->last_gui_command)
    {
        int ret;

        *is_finished = tgdb_can_issue_command(tgdb);

        if (tgdb->show_gui_commands)
        {
            strncpy(buf, tgdb->last_gui_command, n);
            ret = strlen(tgdb->last_gui_command);
        }
        else
        {
            buf[0] = '\n';
            ret = 1;
        }

        free(tgdb->last_gui_command);
        tgdb->last_gui_command = NULL;
        return ret;
    }

    if (tgdb->has_sigchld_recv)
    {
        int tgdb_will_quit;

        /* tgdb_get_quit_command will return right away, it's asynchrounous.
         * We call it to determine if it was GDB that died.
         * If GDB didn't die, things will work like normal. ignore this.
         * If GDB did die, this get's the quit command and add's it to the list. It's
         * OK that the rest of this function get's executed, since the read will simply
         * return EOF.
         */
        int val = tgdb_get_quit_command(tgdb, &tgdb_will_quit);

        if (val == -1)
        {
            clog_error(CLOG_CGDB, "tgdb_get_quit_command error");
            return -1;
        }

        tgdb->has_sigchld_recv = 0;
        if (tgdb_will_quit)
            goto tgdb_finish;
    }

    /* set buf to null for debug reasons */
    memset(buf, '\0', n);

    /* 1. read all the data possible from gdb that is ready. */
    if ((size = io_read(tgdb->debugger_stdout, local_buf, n)) < 0)
    {
        clog_error(CLOG_CGDB, "could not read from masterfd");
        buf_size = -1;
        tgdb_add_quit_command(tgdb);
        goto tgdb_finish;
    }
    else if (size == 0)
    {
        /* EOF */
        tgdb_add_quit_command(tgdb);
        goto tgdb_finish;
    }

    /* 2. At this point local_buf has everything new from this read.
     * Basically this function is responsible for separating the annotations
     * that gdb writes from the data. 
     *
     * buf and buf_size are the data to be returned from the user.
     */

    /* Reset command_finished var. This will get set to 1
       when prompt annotation is parsed. */
    tgdb->a2->command_finished = 0;

    local_buf[size] = '\0';
    a2_parse_io(tgdb->a2, local_buf, size, buf, &buf_size);

    tgdb_process_client_commands(tgdb);

    if (tgdb->a2->command_finished == 1)
    {
        /* success, and finished command */
        tgdb->is_gdb_ready_for_next_command = 1;
    }

    /* 3. if ^c has been sent, clear the buffers.
     *        If a signal has been received, clear the queue and return
     */
    if (tgdb_handle_signals(tgdb) == -1)
    {
        clog_error(CLOG_CGDB, "tgdb_handle_signals failed");
        return -1;
    }

    /* 4. runs the users buffered command if any exists */
    if (tgdb_has_command_to_run(tgdb))
        tgdb_unqueue_and_deliver_command(tgdb);

tgdb_finish:
    *is_finished = tgdb_can_issue_command(tgdb);

    return buf_size;
}

/* Getting Data out of TGDB {{{*/

struct tgdb_response *tgdb_get_response(struct tgdb *tgdb, int i)
{
    struct tgdb_response *response = NULL;

    if (i < sbcount(tgdb->a2->responses))
    {
        /* Return pointer to this response */
        response = tgdb->a2->responses[i];

        /* Try to find the request that initiated this response */
        response->request = tgdb_pop_request_id(response->result_id);
    }

    return response;
}

struct tgdb_response *tgdb_create_response(struct annotate_two *a2,
        enum tgdb_reponse_type header)
{
    struct tgdb_response *response;

    response = (struct tgdb_response *)cgdb_calloc(1, sizeof(struct tgdb_response));
    response->result_id = -1;
    response->request = NULL;
    response->header = header;

    /* Add this response to our list of responses */
    sbpush(a2->responses, response);
    return response;
}

int tgdb_delete_response(struct tgdb_response *com)
{
    if (!com)
        return -1;

    if (com->request)
    {
        tgdb_request_destroy(com->request);
        com->request = NULL;
    }

    switch (com->header)
    {
    case TGDB_UPDATE_BREAKPOINTS:
    {
        int i;

        for (i = 0; i < sbcount(com->choice.update_breakpoints.breakpoints); i++)
        {

            struct tgdb_breakpoint *tb = &com->choice.update_breakpoints.breakpoints[i];

            free(tb->file);
            free(tb->funcname);
        }

        sbfree(com->choice.update_breakpoints.breakpoints);
        com->choice.update_breakpoints.breakpoints = NULL;
        break;
    }
    case TGDB_UPDATE_FILE_POSITION:
    {
        struct tgdb_file_position *tfp =
            com->choice.update_file_position.file_position;

        if (tfp)
        {
            free(tfp->absolute_path);
            free(tfp->from);
            free(tfp->func);

            free(tfp);

            com->choice.update_file_position.file_position = NULL;
        }
        break;
    }
    case TGDB_UPDATE_SOURCE_FILES:
    {
        int i;
        char **source_files = com->choice.update_source_files.source_files;

        for (i = 0; i < sbcount(source_files); i++)
        {
            free(source_files[i]);
        }
        sbfree(source_files);

        com->choice.update_source_files.source_files = NULL;
        break;
    }
    case TGDB_INFERIOR_EXITED:
        break;
    case TGDB_UPDATE_COMPLETIONS:
    {
        int i;
        char **completions = com->choice.update_completions.completions;

        for (i = 0; i < sbcount(completions); i++)
            free(completions[i]);
        sbfree(completions);

        com->choice.update_completions.completions = NULL;
        break;
    }
    case TGDB_UPDATE_DISASSEMBLY:
    {
        int i;
        struct tgdb_response_disassemble *response =
                &com->choice.update_disassemble;
        char **disasm = response->disasm;

        free(response->error_msg);

        for (i = 0; i < sbcount(disasm); i++)
            free(disasm[i]);
        sbfree(disasm);

        response->error_msg = NULL;
        response->disasm = NULL;
        break;
    }
    case TGDB_UPDATE_CONSOLE_PROMPT_VALUE:
    {
        const char *value =
            com->choice.update_console_prompt_value.prompt_value;

        free((char *)value);
        com->choice.update_console_prompt_value.prompt_value = NULL;
        break;
    }
    case TGDB_QUIT:
        break;
    }

    free(com);
    com = NULL;
    return 0;
}

void a2_delete_responses(struct annotate_two *a2)
{
    int i;

    for (i = 0; i < sbcount(a2->responses); i++)
        tgdb_delete_response(a2->responses[i]);

    sbsetcount(a2->responses, 0);
}

/* }}}*/

/* Inferior tty commands {{{*/

int tgdb_tty_new(struct tgdb *tgdb)
{
    int ret = a2_open_new_tty(tgdb->a2,
        &tgdb->inferior_stdin,
        &tgdb->inferior_stdout);

    tgdb_process_client_commands(tgdb);

    return ret;
}

int tgdb_get_inferior_fd(struct tgdb *tgdb)
{
    return tgdb->inferior_stdout;
}

const char *tgdb_tty_name(struct tgdb *tgdb)
{
    return pty_pair_get_slavename(tgdb->a2->pty_pair);
}

/* }}}*/

/* Functional commands {{{ */

/* Request {{{*/

tgdb_request_ptr
tgdb_request_run_console_command(struct tgdb *tgdb, const char *command)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_CONSOLE_COMMAND;
    request_ptr->choice.console_command.command = (const char *)
        cgdb_strdup(command);

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

tgdb_request_ptr tgdb_request_inferiors_source_files(struct tgdb *tgdb)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_INFO_SOURCES;

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

tgdb_request_ptr
tgdb_request_run_debugger_command(struct tgdb *tgdb, enum tgdb_command_type c)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_DEBUGGER_COMMAND;
    request_ptr->choice.debugger_command.c = c;

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

tgdb_request_ptr
tgdb_request_modify_breakpoint(struct tgdb *tgdb, const char *file, int line,
    uint64_t addr, enum tgdb_breakpoint_action b)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_MODIFY_BREAKPOINT;
    request_ptr->choice.modify_breakpoint.file = file ? cgdb_strdup(file) : NULL;
    request_ptr->choice.modify_breakpoint.line = line;
    request_ptr->choice.modify_breakpoint.addr = addr;
    request_ptr->choice.modify_breakpoint.b = b;

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

tgdb_request_ptr tgdb_request_complete(struct tgdb *tgdb, const char *line)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_COMPLETE;
    request_ptr->choice.complete.line = (const char *)cgdb_strdup(line);

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

tgdb_request_ptr tgdb_request_disassemble(struct tgdb *tgdb, uint64_t addr, int lines,
    struct tgdb_file_position *tfp)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_DISASSEMBLE;
    request_ptr->choice.disassemble.addr = addr;
    request_ptr->choice.disassemble.lines = lines;
    request_ptr->choice.disassemble.tfp = tfp;

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

tgdb_request_ptr tgdb_request_disassemble_func(struct tgdb *tgdb,
    enum disassemble_func_type type, const char *file, const char *function,
    struct tgdb_file_position *tfp)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_DISASSEMBLE_FUNC;
    request_ptr->choice.disassemble_func.raw = (type == DISASSEMBLE_FUNC_RAW_INSTRUCTIONS);
    request_ptr->choice.disassemble_func.source = (type == DISASSEMBLE_FUNC_SOURCE_LINES);
    request_ptr->choice.disassemble_func.file = file;
    request_ptr->choice.disassemble_func.function = function;
    request_ptr->choice.disassemble_func.tfp = tfp;

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

tgdb_request_ptr tgdb_request_breakpoints(struct tgdb *tgdb)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_BREAKPOINTS;

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

tgdb_request_ptr tgdb_request_frame(struct tgdb *tgdb)
{
    tgdb_request_ptr request_ptr;

    request_ptr = (tgdb_request_ptr)cgdb_malloc(sizeof(struct tgdb_request));

    request_ptr->id = -1;
    request_ptr->header = TGDB_REQUEST_FRAME;

    handle_request(tgdb, request_ptr);
    return request_ptr;
}

/* }}}*/

/* Process {{{*/

int tgdb_process_command(struct tgdb *tgdb, tgdb_request_ptr request)
{
    if (!tgdb || !request)
        return -1;

    if (!tgdb_can_issue_command(tgdb))
        return -1;

    if (request->header == TGDB_REQUEST_CONSOLE_COMMAND)
    {
        tgdb_send(tgdb, request->choice.console_command.command,
            TGDB_COMMAND_CONSOLE);
    }
    else if (request->header == TGDB_REQUEST_DEBUGGER_COMMAND)
    {
        tgdb_send(tgdb, tgdb_get_client_command(tgdb,
            request->choice.debugger_command.c), TGDB_COMMAND_FRONT_END);
    }
    else if (request->header == TGDB_REQUEST_MODIFY_BREAKPOINT)
    {
        char *val = tgdb_client_modify_breakpoint_call(tgdb,
            request->choice.modify_breakpoint.file,
            request->choice.modify_breakpoint.line,
            request->choice.modify_breakpoint.addr,
            request->choice.modify_breakpoint.b);
        if (val)
        {
            tgdb_send(tgdb, val, TGDB_COMMAND_FRONT_END);
            free(val);
        }
    }
    else
    {
        if (request->header == TGDB_REQUEST_FRAME)
        {
            commands_issue_command(tgdb->a2,
                ANNOTATE_INFO_FRAME, NULL, 1, NULL);
        }
        else if (request->header == TGDB_REQUEST_BREAKPOINTS)
        {
            commands_issue_command(tgdb->a2,
                ANNOTATE_INFO_BREAKPOINTS, NULL, 0, NULL);
        }
        else if (request->header == TGDB_REQUEST_INFO_SOURCES)
        {
            commands_issue_command(tgdb->a2,
                ANNOTATE_INFO_SOURCES, NULL, 0, &request->id);
        }
        else if (request->header == TGDB_REQUEST_COMPLETE)
        {
            commands_issue_command(tgdb->a2,
                ANNOTATE_COMPLETE, request->choice.complete.line, 0,
                &request->id);
        }
        else if (request->header == TGDB_REQUEST_DISASSEMBLE)
        {
            tgdb_disassemble(tgdb->a2, request->choice.disassemble.addr,
                request->choice.disassemble.lines, &request->id);
        }
        else if (request->header == TGDB_REQUEST_DISASSEMBLE_FUNC)
        {
            tgdb_disassemble_func(tgdb->a2,
                request->choice.disassemble_func.raw,
                request->choice.disassemble_func.source,
                request->choice.disassemble_func.file,
                request->choice.disassemble_func.function,
                &request->id);
        }

        tgdb_process_client_commands(tgdb);
    }

    tgdb_set_last_request(request);
    return 0;
}

/* }}}*/

/* }}} */

/* TGDB Queue commands {{{*/

int tgdb_queue_append(struct tgdb *tgdb, tgdb_request_ptr request)
{
    if (!tgdb || !request)
        return -1;

    sbpush(tgdb->gdb_client_request_queue, request);
    return 0;
}

tgdb_request_ptr tgdb_queue_pop(struct tgdb *tgdb)
{
    return sbpopfront(tgdb->gdb_client_request_queue);
}

int tgdb_queue_size(struct tgdb *tgdb)
{
    return sbcount(tgdb->gdb_client_request_queue);
}

/* }}}*/

/* Signal Handling Support {{{*/

int tgdb_signal_notification(struct tgdb *tgdb, int signum)
{
    struct termios t;
    cc_t *sig_char = NULL;

    tcgetattr(tgdb->debugger_stdin, &t);

    if (signum == SIGINT)
    {
        /* ^c */
        tgdb->control_c = 1;
        sig_char = &t.c_cc[VINTR];
        if (write(tgdb->debugger_stdin, sig_char, 1) < 1)
            return -1;
    }
    else if (signum == SIGQUIT)
    {
        /* ^\ */
        sig_char = &t.c_cc[VQUIT];
        if (write(tgdb->debugger_stdin, sig_char, 1) < 1)
            return -1;
    }
    else if (signum == SIGCHLD)
    {
        tgdb->has_sigchld_recv = 1;
    }

    return 0;
}

/* }}}*/

/* Config Options {{{*/
int tgdb_set_verbose_gui_command_output(struct tgdb *tgdb, int value)
{
    tgdb->show_gui_commands = !!value;

    return value;
}

/* }}}*/
