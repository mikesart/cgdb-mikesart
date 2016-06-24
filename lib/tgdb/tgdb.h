#ifndef __TGDB_H__
#define __TGDB_H__

/*! 
 * \file
 * tgdb.h
 *
 * \brief
 * This interface is intended to be the abstraction layer between a front end 
 * and the low level debugger the front end is trying to communicate with.
 */

/* includes {{{*/
#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#include "tgdb_types.h"
/* }}}*/

/* Creating and Destroying a libtgdb context. {{{*/
/******************************************************************************/
/**
 * @name Createing and Destroying a libtgdb context.
 * These functions are for createing and destroying a tgdb context.
 */
/******************************************************************************/

/*@{*/

/**
   *  This struct is a reference to a libtgdb instance.
   */
struct tgdb;

/**
   * This initializes a tgdb library instance. It starts up the debugger and 
   * returns all file descriptors the client must select on.
   *
   * The client must call this function before any other function in the 
   * tgdb library.
   *
   * \param debugger
   * The path to the desired debugger to use. If this is NULL, then just
   * "gdb" is used.
   *
   * \param argc
   * The number of arguments to pass to the debugger
   *
   * \param argv
   * The arguments to pass to the debugger    
   *
   * \param debugger_fd
   * The descriptor to the debugger's I/O
   *
   * @return
   * NULL on error, a valid context on success.
   */
struct tgdb *tgdb_initialize(const char *debugger,
    int argc, char **argv, int *debugger_fd);

/**
   * This will terminate a libtgdb session. No functions should be called on
   * the tgdb context passed into this function after this call.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * @return
   * 0 on success or -1 on error
   */
int tgdb_shutdown(struct tgdb *tgdb);

/*@}*/
/* }}}*/

/* Status Commands {{{*/
/******************************************************************************/
/**
 * @name Status Commands
 * These functions are for querying the tgdb context.
 */
/******************************************************************************/

/*@{*/

/**
   * This will check to see if TGDB is currently capable of receiving another command.
   * 
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \return
   * Will return as 1 if tgdb is busy, otherwise 0.
   */
int tgdb_is_busy(struct tgdb *tgdb);

/*@}*/
/* }}}*/

/* Input/Output commands {{{*/
/******************************************************************************/
/**
 * @name Input/Output commands
 * These functions are for communicating I/O with the tgdb context.
 */
/******************************************************************************/

/*@{*/

/**
   * Have TGDB process a command.
   * 
   * \param tgdb
   * An instance of the tgdb library to operate on.
   * 
   * \param request
   * The requested command to have TGDB process.
   *
   * \return
   * 0 on success or -1 on error
   */
int tgdb_process_command(struct tgdb *tgdb, tgdb_request_ptr request);

/**
   * This function does most of the dirty work in TGDB. It is capable of 
   * processing the output of the debugger, to either satisfy a previously 
   * made request, or to simply get console output for the caller to have.
   *
   * The data returned from this function is the console output of the 
   * debugger.
   * 
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param buf
   * The output of the debugger will be returned in this buffer.
   * The buffer passed back will not exceed N in size.
   *
   * \param n
   * Tells libtgdb how large the buffer BUF is that the client passed in.
   *
   * \param is_finished
   * If this is passed in as NULL, it is not set.
   *
   * If it is non-null, it will be set to 1 if TGDB finished processing the
   * current request. Otherwise, it will be set to 0 if TGDB needs more input
   * in order to finish processing the current requested command.
   *
   * @return
   * The number of valid bytes in BUF on success, or -1 on error.
   */
size_t tgdb_process(struct tgdb *tgdb, char *buf, size_t n,
    int *is_finished);

/**
   * This sends a byte of data to the program being debugged.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param c
   * The character to pass to the program being debugged.
   *
   * @return
   * 0 on success or -1 on error
   */
int tgdb_send_inferior_char(struct tgdb *tgdb, char c);

/**
   * Gets the output from the program being debugged.
   * 
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param buf
   * The output of the program being debugged will be returned here.
   *
   * \param n
   * Tells libtgdb how large the buffer BUF is.
   *
   * @return
   * The number of valid bytes in BUF on success, or 0 on error.
   */
ssize_t tgdb_recv_inferior_data(struct tgdb *tgdb, char *buf, size_t n);

/*@}*/
/* }}}*/

/* Getting Data out of TGDB {{{*/
/******************************************************************************/
/**
 * @name Getting Data out of TGDB
 * These functions are for dealing with getting back data from TGDB
 */
/******************************************************************************/

/*@{*/

/**
   * Gets a response from TGDB.
   * This should be called after tgdb_recv_debugger_data
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * @return
   * A valid response if responses still exist.
   * Null if no more responses exist.
   */
struct tgdb_response *tgdb_get_response(struct tgdb *tgdb);

/**
   * This will traverse all of the responses that the context tgdb currently
   * has and will print them. It is currently used for debugging purposes.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   */
void tgdb_traverse_responses(struct tgdb *tgdb);

/**
   * This will free all of the memory used by the responses that tgdb returns.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   */
void tgdb_delete_responses(struct tgdb *tgdb);

/*@}*/
/* }}}*/

/* Inferior tty commands {{{*/
/******************************************************************************/
/**
 * @name Inferior tty commands
 * These functinos are used to alter the tty state of the inferior program.
 * These functions currently are not always supported. If the annotate_two
 * subsytem is being used, the tty commands are supported.
 */
/******************************************************************************/

/*@{*/

/**
   * This allocates a new tty and tells the debugger to use it for I/O
   * with the program being debugged.
   *
   * Whatever was left in the old tty is lost, the debugged program starts
   * with a new tty.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * NOTE
   *  The return value only indicates whether the tty was allocated properly,
   *  not whether the debugger accepted the tty, since this can only be determined 
   *  when the debugger responds, not when the command is given.
   *
   * @return
   * 0 on success or -1 on error
   */
int tgdb_tty_new(struct tgdb *tgdb);

/**
   * Get the file descriptor the debugger is using for the inferior.
   *
   * You can see the associated terminal name for the file descriptor
   * with the following gdb command,
   *   (gdb) show inferior-tty
   *   Terminal for future runs of program being debugged is "/dev/pts/34".
   *
   * @tgdb
   * An instance of the tgdb library to operate on.
   *
   * @return
   * The descriptor to the I/O of the program being debugged (-1 on error).
   */
int tgdb_get_inferior_fd(struct tgdb *tgdb);

/**
   * Gets the name of file that debugger is using for I/O with the program
   * being debugged.
   * 
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * @return
   * Name of tty or NULL on error.
   */
const char *tgdb_tty_name(struct tgdb *tgdb);

/*@}*/
/* }}}*/

/* Functional commands {{{*/
/******************************************************************************/
/**
 * @name Functional commands
 * These functinos are used to ask the TGDB context to perform a task.
 */
/******************************************************************************/

/*@{*/

/**
   * This sends a console command to the debugger (GDB).
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param command
   * The null terminated command to pass to GDB as a console command.
   * 
   * \return
   * Will return as a tgdb request command on success, otherwise NULL.
   */
tgdb_request_ptr tgdb_request_run_console_command(struct tgdb *tgdb,
    const char *command);

/**
   * Gets a list of source files that make up the program being debugged.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \return
   * Will return as a tgdb request command on success, otherwise NULL.
   */
tgdb_request_ptr tgdb_request_inferiors_source_files(struct tgdb *tgdb);

/**
   * This tells libtgdb to run a command through the debugger.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param c
   * This is the command that libtgdb should run through the debugger.
   *
   * @return
   * Will return as a tgdb request command on success, otherwise NULL.
   */
tgdb_request_ptr tgdb_request_run_debugger_command(struct tgdb *tgdb,
    enum tgdb_command_type c);

/**
   * Modify's a breakpoint.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param file
   * The file to set the breakpoint in.
   *
   * \param line
   * The line in FILE to set the breakpoint in.
   *
   * \param b
   * Determines what the user wants to do with the breakpoint.
   *
   * @return
   * Will return as a tgdb request command on success, otherwise NULL.
   */
tgdb_request_ptr tgdb_request_modify_breakpoint(struct tgdb *tgdb,
    const char *file, int line, enum tgdb_breakpoint_action b);

/**
   * Used to get all of the possible tab completion options for LINE.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param line
   * The line to tab complete.
   *
   * \return
   * Will return as a tgdb request command on success, otherwise NULL.
   */
tgdb_request_ptr tgdb_request_complete(struct tgdb *tgdb, const char *line);

tgdb_request_ptr tgdb_request_disassemble(struct tgdb *tgdb,
    const char *func, int lines,
    struct tgdb_file_position *tfp);

tgdb_request_ptr tgdb_request_breakpoints(struct tgdb *tgdb);

/**
   * Used to get disassembly for function.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \return
   * Will return as a tgdb request command on success, otherwise NULL.
   */
enum disassemble_func_type
{
    DISASSEMBLE_FUNC_DISASSEMBLY,
    DISASSEMBLE_FUNC_SOURCE_LINES,
    DISASSEMBLE_FUNC_RAW_INSTRUCTIONS,
};
tgdb_request_ptr tgdb_request_disassemble_func(struct tgdb *tgdb,
    enum disassemble_func_type type, const char *file, const char *function,
    struct tgdb_file_position *tfp);

/**
   * Free the tgdb request pointer data.
   *
   * \param request_ptr
   * Request pointer from tgdb_request_* function to destroy.
   */
void tgdb_request_destroy(tgdb_request_ptr request_ptr);

void tgdb_set_last_request(struct tgdb_request *request);
int tgdb_last_request_requires_update();

tgdb_request_ptr tgdb_pop_request_id(int id);

/*@}*/
/* }}}*/

/* TGDB Queue commands {{{*/
/******************************************************************************/
/**
 * @name Queuing of TGDB commands
 * These functinos are used to allow clients to queue multiple commands for 
 * TGDB to be able to run.
 */
/******************************************************************************/

/**
   * Have TGDB append a command to it's list of commands it needs to run.
   * 
   * \param tgdb
   * An instance of the tgdb library to operate on.
   * 
   * \param request
   * The requested command to have TGDB process.
   *
   * \return
   * 0 on success or -1 on error
   */
int tgdb_queue_append(struct tgdb *tgdb, tgdb_request_ptr request);

/**
   * Get a tgdb_request command back from TGDB.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \return
   * The requested command at the top of the stack, or NULL on error.
   * Popping a command when the queue is empty is also considered an error,
   * and NULL will be returned.
   */
tgdb_request_ptr tgdb_queue_pop(struct tgdb *tgdb);

/**
   * Get's the number of items that are in the queue for TGDB to run.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \return
   * The number of items in the list.
   */
int tgdb_queue_size(struct tgdb *tgdb);

/* }}}*/

/* Signal Handling Support {{{*/
/******************************************************************************/
/**
 * @name Signal Handling Support
 * These functinos are used to notify TGDB of signal received.
 */
/******************************************************************************/

/*@{*/

/**
   * The front end can use this function to notify libtgdb that an
   * asynchronous event has occurred. If signal SIGNUM is relavant
   * to libtgdb, the appropriate processing will be done.
   * Currently, TGDB only handles SIGINT,SIGTERM and SIGQUIT.
   *
   * libtgdb will remove all elements from it's queue when a SIGINT
   * is received.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param signum
   * The signal number SIGNUM that has occurred.
   *
   * @return
   * 0 on success or -1 on error
   */
int tgdb_signal_notification(struct tgdb *tgdb, int signum);

/*@}*/
/* }}}*/

/* Config Options {{{*/
/******************************************************************************/
/**
 * @name TGDB Config Options
 * These functinos are used to change the state TGDB works. TGDB can currently
 * only be configured through this interface.
 */
/******************************************************************************/

/*@{*/

/**
   * This sets the verbosity of the GUI's commands.
   * If the value is set to 0, the GUI's commands will not be shown.
   * If the value is set to 1, the GUI's commands will be shown.
   * The default value for TGDB is to be off.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param value
   * 0 to not show GUI commands, 1 to show them, otherwise nothing is done.
   * You would use a value other than 0 or 1 just to query if the option is set.
   *
   * @return
   * 1 if option is set, otherwise 0
   */
int tgdb_set_verbose_gui_command_output(struct tgdb *tgdb, int value);

/**
   * This will make TGDB handle error's in a verbose mode.
   * The basically mean's that when TGDB find's an error, the message is
   * printed to stdout/stderr. Normally this is not acceptable because TGDB can
   * simple not print when any front end is using it. Imparticular, TGDB can
   * not print when a curses based front end is using it.
   *
   * \param tgdb
   * An instance of the tgdb library to operate on.
   *
   * \param value
   * if -1, query to see if the option is set.
   * If 0, the option will be turned off.
   * If 1, the option will be turned on.
   *
   * @return
   * 1 if option is set, otherwise 0
   */
int tgdb_set_verbose_error_handling(struct tgdb *tgdb, int value,
    void (*print_message)(const char *fmt, ...));

/*@}*/
/* }}}*/

#endif /* __TGDB_H__ */
