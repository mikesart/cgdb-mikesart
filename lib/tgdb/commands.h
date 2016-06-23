#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include "tgdb_types.h"
#include "a2-tgdb.h"

/* commands_issue_command:
 * -----------------------
 *  
 *  This is used by the annotations library to internally issure commands to
 *  the debugger. It sends a command to tgdb-base.
 *
 *  Returns -1 on error, 0 on success
 */
int commands_issue_command(struct tgdb_list *client_command_list,
    enum annotate_commands commmand, const char *data, int oob, int *id);

/* commands_process: This function receives the output from gdb when gdb
 *                   is running a command on behalf of this package.
 *
 *    a     -> the character received from gdb.
 *    com   -> commands to give back to gdb.
 */
void commands_process_cgdb_gdbmi(struct annotate_two *a2, struct ibuf *buf,
    int result_record, char *result_line, int id,
    struct tgdb_list *command_list);

/**
 * The current command type. TGDB is capable of having any commands of this
 * type in it's queue.
 *
 * I don't know what this should look like completely.
 */
enum tgdb_command_choice
{
    /** A command from the front end */
    TGDB_COMMAND_FRONT_END,

    /** A command from the console */
    TGDB_COMMAND_CONSOLE,

    /** A command from a client of TGDB */
    TGDB_COMMAND_TGDB_CLIENT,

    /**
     * A priority command is a command that the client context needs
     * to run before it can finish getting the data necessary for a
     * TGDB_COMMAND_TGDB_CLIENT command.
     */
    TGDB_COMMAND_TGDB_CLIENT_PRIORITY
};

/* This is here to add new client_command/command faster */

/**
 * This is the main tgdb queue command.
 * It will be put into a queue and run.
 * For each TGDB command generated by a client, the client command is
 * with it.
 */
struct tgdb_command
{
    /** The actual command to give. */
    char *gdb_command;

    /** The type of command this one is. */
    enum tgdb_command_choice command_choice;

    /** Private data the client context can use. */
    enum annotate_commands command;
};

/**
 * This will free a TGDB queue command.
 * These are the commands given by TGDB to the debugger.
 * This is a function for debugging.
 *
 * \param item
 * The command to free
 */
void tgdb_command_destroy(void *item);

/**
 * Get gdb version, major and minor numbers.
 * Ie, major:7, minor:10
 *
 * Returns 1 on success, 0 on failure.
 */
int tgdb_get_gdb_version(int *major, int *minor);

#endif
