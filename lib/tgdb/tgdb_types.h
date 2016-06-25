#ifndef __TGDB_TYPES_H__
#define __TGDB_TYPES_H__

/*! 
 * \file
 * tgdb_types.h
 *
 * \brief
 * This interface is intended to declare and document the ADT's that TGDB 
 * exports to the front ends.
 *
 * The front end can interrogate these data structures to discover what TGDB
 * knows about the debugger. This is currently the only way the front end gets
 * any information about the current debugging session.
 */

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

/**
  * The client can give any of these commands to TGDB through 
  * tgdb_run_debugger_command.
  */
enum tgdb_command_type
{
    /** This will instruct TGDB to tell the debugger to continue.  */
    TGDB_CONTINUE = 0,
    /** This will instruct TGDB to tell the debugger to finish.  */
    TGDB_FINISH,
    /** 
     * This will instruct TGDB to tell the debugger to go to the next 
     * source level instruction.
     */
    TGDB_NEXT,
    /** This will instruct TGDB to tell the debugger to (re-)start the program. */
    TGDB_START,
    /** This will instruct TGDB to tell the debugger to (re-)run the program. */
    TGDB_RUN,
    /** This will instruct TGDB to tell the debugger to kill the program. */
    TGDB_KILL,
    /** This will instruct TGDB to tell the debugger to step. */
    TGDB_STEP,
    /** 
     * This will instruct TGDB to tell the debugger to continue running
     * until a source line past the current line.  This is used to avoid
     * single stepping through loops.
     */
    TGDB_UNTIL,
    /** This will instruct TGDB to tell the debugger to go up a frame. */
    TGDB_UP,
    /** This will instruct TGDB to tell the debugger to go down a frame. */
    TGDB_DOWN,
};

/**
  * This gives the client the ability to add or remove breakpoints.
  * Currently, enable/disable are not supported.
  */
enum tgdb_breakpoint_action
{
    /** Add a breakpoint. */
    TGDB_BREAKPOINT_ADD,
    /** Add a temporary breakpoint */
    TGDB_TBREAKPOINT_ADD,
    /** Delete a breakpoint. */
    TGDB_BREAKPOINT_DELETE,
};

/** This structure represents a breakpoint. */
struct tgdb_breakpoint
{
    /** This is the fullname to the file that the breakpoint is set in. */
    char *file;
    /** The name of the function the breakpoint is set at. */
    char *funcname;
    /** The line number where the breakpoint is set. */
    int line;
    /** breakpoint address */
    uint64_t addr;
    /** 0 if it is not enabled or 1 if it is enabled. */
    int enabled;
};

/**
  * This structure currently represents a file position.
  * Info from gdbmi -stack-info-frame call.
  */
struct tgdb_file_position
{
    /** The absolute path to the file.  */
    char *absolute_path;
    /** The line number in the file.  */
    int line_number;
    /** Line number corresponding to the $pc.  */
    uint64_t addr;
    /** Shared library where this function is defined.  */
    char *from;
    /** Function name.  */
    char *func;
};

enum tgdb_request_type
{
    /** Request for TGDB to run a console command through the debugger */
    TGDB_REQUEST_CONSOLE_COMMAND,
    /**
     * Request for TGDB to get all of the source files that the debugger 
     * currently knows about the inferior. */
    TGDB_REQUEST_INFO_SOURCES,
    /** Run a debugger command (ie next, step, finish) */
    TGDB_REQUEST_DEBUGGER_COMMAND,
    /** Modify a breakpoint (ie delete/create/disable) */
    TGDB_REQUEST_MODIFY_BREAKPOINT,
    /** Ask GDB to give a list of tab completions for a given string */
    TGDB_REQUEST_COMPLETE,
    /** Ask GDB to disassemble an address */
    TGDB_REQUEST_DISASSEMBLE,
    /** Ask GDB to disassemble a function */
    TGDB_REQUEST_DISASSEMBLE_FUNC,
    /** Ask GDB to update breakpoints */
    TGDB_REQUEST_BREAKPOINTS,
    /** Ask GDB to update inferior frame information */
    TGDB_REQUEST_FRAME,
};

struct tgdb_request
{
    /** This is the gdbmi request id number. */
    int id;

    /** This is the type of request. */
    enum tgdb_request_type header;

    union {
        struct
        {
            /** The null terminated console command to pass to GDB */
            const char *command;
        } console_command;

        struct
        {
            /** This is the command that libtgdb should run through the debugger */
            enum tgdb_command_type c;
        } debugger_command;

        struct
        {
            /* The filename to set the breakpoint in */
            const char *file;
            /* The corresponding line number */
            int line;
            /* The address to set breakpoint in (if file is null). */
            uint64_t addr;
            /* The action to take */
            enum tgdb_breakpoint_action b;
        } modify_breakpoint;

        struct
        {
            /* The line to ask GDB for completions for */
            const char *line;
        } complete;

        struct
        {
            uint64_t addr;
            int lines;
            struct tgdb_file_position *tfp;
        } disassemble;

        struct
        {
            int source;
            int raw;
            const char *file;
            const char *function;
            struct tgdb_file_position *tfp;
        } disassemble_func;
    } choice;
};

typedef struct tgdb_request *tgdb_request_ptr;

/**
  *  This is the commands interface used between the front end and TGDB.
  *  When TGDB is responding to a request or when an event is being generated
  *  the front end will find out about it through one of these enums.
  */
enum tgdb_reponse_type
{
    /** All breakpoints that are set. */
    TGDB_UPDATE_BREAKPOINTS,

    /**
     * This tells the gui what filename/line number the debugger is on.
     * It gets generated whenever it changes.
     * This is a 'struct tgdb_file_position *'.
      */
    TGDB_UPDATE_FILE_POSITION,

    /**
     * This returns a list of all the source files that make up the 
     * inferior program.
     */
    TGDB_UPDATE_SOURCE_FILES,

    /**
     * This happens when the program being debugged by GDB exits. 
     * It can be used be the front end to clear any cache that it might have
     * obtained by debugging the last program. The data represents the exit
     * status.
     */
    TGDB_INFERIOR_EXITED,

    /** This returns a list of all the completions. */
    TGDB_UPDATE_COMPLETIONS,

    /** Disassemble output */
    TGDB_UPDATE_DISASSEMBLY,

    /** The prompt has changed, here is the new value.  */
    TGDB_UPDATE_CONSOLE_PROMPT_VALUE,

    /**
     * This happens when gdb quits.
     * libtgdb is done. 
     * You will get no more responses after this one.
     * This is a 'struct tgdb_quit_status *'
     */
    TGDB_QUIT
};

/* header == TGDB_UPDATE_BREAKPOINTS */
struct tgdb_response_breakpoints
{
    /* This list has elements of 'struct tgdb_breakpoint *'
     * representing each breakpoint. */
    struct tgdb_breakpoint *breakpoints;
};

/* header == TGDB_UPDATE_FILE_POSITION */
struct tgdb_response_file_position
{
    struct tgdb_file_position *file_position;
};

/* header == TGDB_UPDATE_SOURCE_FILES */
struct tgdb_response_source_files
{
    /* This list has elements of 'const char *' representing each
     * filename. The filename may be relative or absolute. */
    char **source_files;
};

/* header == TGDB_INFERIOR_EXITED */
struct tgdb_response_exited
{
    int exit_status;
};

/* header == TGDB_UPDATE_COMPLETIONS */
struct tgdb_response_completions
{
    /* This list has elements of 'const char *'
     * representing each possible completion. */
    struct tgdb_list *completion_list;
};

/* header == TGDB_UPDATE_DISASSEMBLY */
struct tgdb_response_disassemble
{
    uint64_t addr_start;
    uint64_t addr_end;
    /* Error message string */
    char *error_msg;
    /* True if we tried to disassemble entire function using
       gdb disassemble command */
    int is_disasm_function;
    char **disasm;
};

/* header == TGDB_UPDATE_CONSOLE_PROMPT_VALUE */
struct tgdb_response_prompt_value
{
    /* The new prompt GDB has reported */
    const char *prompt_value;
};

/* header == TGDB_QUIT */
struct tgdb_response_quit
{
    /**
      * This tells the front end how the debugger terminated.
      *
      * If this is 0, the debugger terminated normally and return_value is valid
      * If this is -1, the debugger terminated abnormally and return_value is
      * invalid
      */
    int exit_status;

    /** This is the return value of the debugger upon normal termination. */
    int return_value;
};

/**
  * A single TGDB response for the front end.
  * This is the smallest unit of information that TGDB can return to the front 
  * end.
  */
struct tgdb_response
{
    /** gdb/mi command ID */
    int result_id;

    /** Request that initiated this response (if available). */
    struct tgdb_request *request;

    /** This is the type of response. */
    enum tgdb_reponse_type header;

    union {
        struct tgdb_response_breakpoints update_breakpoints;
        struct tgdb_response_file_position update_file_position;
        struct tgdb_response_source_files update_source_files;
        struct tgdb_response_exited inferior_exited;
        struct tgdb_response_completions update_completions;
        struct tgdb_response_disassemble update_disassemble;
        struct tgdb_response_prompt_value update_console_prompt_value;
        struct tgdb_response_quit quit;
    } choice;
};

struct tgdb_response *tgdb_create_response(struct annotate_two *a2,
        enum tgdb_reponse_type header);

int tgdb_delete_response(struct tgdb_response *response);

#endif /* __TGDB_TYPES_H__ */
