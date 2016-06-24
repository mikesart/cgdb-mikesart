#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#if HAVE_REGEX_H
#include <regex.h>
#endif /* HAVE_REGEX_H */

/* Local includes */
#include "commands.h"
#include "data.h"
#include "io.h"
#include "tgdb_types.h"
#include "globals.h"
#include "logger.h"
#include "sys_util.h"
#include "queue.h"
#include "ibuf.h"
#include "a2-tgdb.h"
#include "queue.h"
#include "tgdb_list.h"
#include "annotate_two.h"
#include "mi_gdb.h"

/**
 * This structure represents most of the I/O parsing state of the 
 * annotate_two subsytem.
 */
struct commands {

  /** The state of the command context.  */
    enum COMMAND_STATE cur_command_state;

  /**
   * This is related to parsing the breakpoint annotations.
   * It keeps track of the current field we are in.
   */
    int cur_field_num;

  /** breakpoint information */
    /*@{ */

  /** A list of breakpoints already parsed.  */
    struct tgdb_list *breakpoint_list;

  /** The current breakpoint being parsed.  */
    struct ibuf *breakpoint_string;

    /*@} */

  /** 'info source' information */

    /*@{ */

  /** The current info source line being parsed */
    struct ibuf *info_source_string;

  /** The current line number the debugger is at in the inferior.  */
    struct ibuf *line_number;

  /** The discovered relative path, found from the info source output.  */
    struct ibuf *info_source_relative_path;

  /** The discovered absolute path, found from the info source output.  */
    struct ibuf *info_source_absolute_path;

  /** Finished parsing the line being looked for.  */
    int info_source_ready;

  /** The name of the file requested to have 'info source' run on.  */
    struct ibuf *last_info_source_requested;

    /*@} */

    /* info sources information {{{ */
    /*@{ */

  /** ??? Finished parsing the data being looked for.  */
    int sources_ready;

  /** All of the sources.  */
    struct ibuf *info_sources_string;

  /** All of the source, parsed in put in a list, 1 at a time.  */
    struct tgdb_list *inferior_source_files;

    /*@} */
    /* }}} */

    /* tab completion information {{{ */
    /*@{ */

  /** ??? Finished parsing the data being looked for.  */
    int tab_completion_ready;

  /** A tab completion item */
    struct ibuf *tab_completion_string;

  /** All of the tab completion items, parsed in put in a list, 1 at a time. */
    struct tgdb_list *tab_completions;

    /*@} */
    /* }}} */

  /** The absolute path prefix output by GDB when 'info source' is given */
    const char *source_prefix;

  /** The length of the line above.  */
    int source_prefix_length;

  /** The relative path prefix output by GDB when 'info source' is given */
    const char *source_relative_prefix;

  /** The length of the line above.  */
    int source_relative_prefix_length;
};

struct commands *commands_initialize(void)
{
    struct commands *c = (struct commands *) cgdb_malloc(sizeof (struct commands));

    c->cur_command_state = VOID_COMMAND;
    c->cur_field_num = 0;

    c->breakpoint_list = tgdb_list_init();
    c->breakpoint_string = ibuf_init();

    c->info_source_string = ibuf_init();
    c->line_number = ibuf_init();
    c->info_source_relative_path = ibuf_init();
    c->info_source_absolute_path = ibuf_init();
    c->info_source_ready = 0;
    c->last_info_source_requested = ibuf_init();

    c->sources_ready = 0;
    c->info_sources_string = ibuf_init();
    c->inferior_source_files = tgdb_list_init();

    c->tab_completion_ready = 0;
    c->tab_completion_string = ibuf_init();
    c->tab_completions = tgdb_list_init();

    c->source_prefix = "Located in ";
    c->source_prefix_length = 11;

    c->source_relative_prefix = "Current source file is ";
    c->source_relative_prefix_length = 23;

    return c;
}

int free_breakpoint(void *item)
{
    struct tgdb_breakpoint *bp = (struct tgdb_breakpoint *) item;

    if (bp->file) {
        free(bp->file);
        bp->file = NULL;
    }

    if (bp->funcname) {
        free(bp->funcname);
        bp->funcname = NULL;
    }

    free(bp);
    bp = NULL;

    return 0;
}

int free_char_star(void *item)
{
    char *s = (char *) item;

    free(s);
    s = NULL;

    return 0;
}

void commands_shutdown(struct commands *c)
{
    if (c == NULL)
        return;

    tgdb_list_free(c->breakpoint_list, free_breakpoint);
    tgdb_list_destroy(c->breakpoint_list);

    ibuf_free(c->breakpoint_string);
    c->breakpoint_string = NULL;

    ibuf_free(c->info_source_string);
    c->info_source_string = NULL;

    ibuf_free(c->line_number);
    c->line_number = NULL;

    ibuf_free(c->info_source_relative_path);
    c->info_source_relative_path = NULL;

    ibuf_free(c->info_source_absolute_path);
    c->info_source_absolute_path = NULL;

    ibuf_free(c->last_info_source_requested);
    c->last_info_source_requested = NULL;

    ibuf_free(c->info_sources_string);
    c->info_sources_string = NULL;

    ibuf_free(c->tab_completion_string);
    c->tab_completion_string = NULL;

    tgdb_list_destroy(c->tab_completions);

    tgdb_list_free(c->inferior_source_files, free_char_star);
    tgdb_list_destroy(c->inferior_source_files);

    /* TODO: free source_files queue */

    free(c);
    c = NULL;
}

int
commands_parse_field(struct commands *c, const char *buf, size_t n, int *field)
{
    if (sscanf(buf, "field %d", field) != 1)
        logger_write_pos(logger, __FILE__, __LINE__,
                "parsing field annotation failed (%s)\n", buf);

    return 0;
}

/* source filename:line:character:middle:addr */
int
commands_parse_source(struct commands *c,
        struct tgdb_list *client_command_list,
        const char *buf, size_t n, struct tgdb_list *list)
{
    /* set up the info_source command to get info */
    if (commands_issue_command(c,
                    client_command_list,
                    ANNOTATE_INFO_SOURCE_RELATIVE, NULL, 1) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }

    return 0;
}

void
commands_set_state(struct commands *c,
        enum COMMAND_STATE state, struct tgdb_list *list)
{
    c->cur_command_state = state;
}

enum COMMAND_STATE commands_get_state(struct commands *c)
{
    return c->cur_command_state;
}

static void
commands_prepare_info_source(struct annotate_two *a2, struct commands *c,
        enum COMMAND_STATE state)
{
    data_set_state(a2, INTERNAL_COMMAND);
    ibuf_clear(c->info_source_string);
    ibuf_clear(c->info_source_relative_path);
    ibuf_clear(c->info_source_absolute_path);

    if (state == INFO_SOURCE_FILENAME_PAIR)
        commands_set_state(c, INFO_SOURCE_FILENAME_PAIR, NULL);
    else if (state == INFO_SOURCE_RELATIVE)
        commands_set_state(c, INFO_SOURCE_RELATIVE, NULL);

    c->info_source_ready = 0;
}

void
commands_list_command_finished(struct commands *c,
        struct tgdb_list *list, int success)
{
    /* The file does not exist and it can not be opened.
     * So we return that information to the gui.  */
    struct tgdb_source_file *rejected = (struct tgdb_source_file *)
            cgdb_malloc(sizeof (struct tgdb_source_file));
    struct tgdb_response *response = (struct tgdb_response *)
            cgdb_malloc(sizeof (struct tgdb_response));

    if (c->last_info_source_requested == NULL)
        rejected->absolute_path = NULL;
    else
        rejected->absolute_path =
                strdup(ibuf_get(c->last_info_source_requested));

    response->header = TGDB_ABSOLUTE_SOURCE_DENIED;
    response->choice.absolute_source_denied.source_file = rejected;

    tgdb_types_append_command(list, response);
}

/* This will send to the gui the absolute path to the file being requested. 
 * Otherwise the gui will be notified that the file is not valid.
 */
static void
commands_send_source_absolute_source_file(struct commands *c,
        struct tgdb_list *list)
{
    /*err_msg("Whats up(%s:%d)\r\n", info_source_buf, info_source_buf_pos); */
    unsigned long length = ibuf_length(c->info_source_absolute_path);

    /* found */
    if (length > 0) {
        char *apath = NULL, *rpath = NULL;
        struct tgdb_response *response;

        if (length > 0)
            apath = ibuf_get(c->info_source_absolute_path);
        if (ibuf_length(c->info_source_relative_path) > 0)
            rpath = ibuf_get(c->info_source_relative_path);

        response = (struct tgdb_response *)
                cgdb_malloc(sizeof (struct tgdb_response));
        response->header = TGDB_FILENAME_PAIR;
        response->choice.filename_pair.absolute_path = strdup(apath);
        response->choice.filename_pair.relative_path = strdup(rpath);
        tgdb_types_append_command(list, response);
        /* not found */
    } else {
        struct tgdb_source_file *rejected = (struct tgdb_source_file *)
                cgdb_malloc(sizeof (struct tgdb_source_file));
        struct tgdb_response *response = (struct tgdb_response *)
                cgdb_malloc(sizeof (struct tgdb_response));

        response->header = TGDB_ABSOLUTE_SOURCE_DENIED;

        if (c->last_info_source_requested == NULL)
            rejected->absolute_path = NULL;
        else
            rejected->absolute_path =
                    strdup(ibuf_get(c->last_info_source_requested));

        response->choice.absolute_source_denied.source_file = rejected;
        tgdb_types_append_command(list, response);
    }
}

static void
commands_send_source_relative_source_file(struct commands *c,
        struct tgdb_list *list)
{
    /* So far, INFO_SOURCE_RELATIVE is only used when a 
     * TGDB_UPDATE_FILE_POSITION is needed.
     */
    /* This section allocates a new structure to add into the queue 
     * All of its members will need to be freed later.
     */
    struct tgdb_file_position *tfp = (struct tgdb_file_position *)
            cgdb_malloc(sizeof (struct tgdb_file_position));
    struct tgdb_response *response = (struct tgdb_response *)
            cgdb_malloc(sizeof (struct tgdb_response));

    tfp->absolute_path = strdup(ibuf_get(c->info_source_absolute_path));
    tfp->relative_path = strdup(ibuf_get(c->info_source_relative_path));
    tfp->line_number = atoi(ibuf_get(c->line_number));

    response->header = TGDB_UPDATE_FILE_POSITION;
    response->choice.update_file_position.file_position = tfp;

    tgdb_types_append_command(list, response);
}

/* commands_process_info_source:
 * -----------------------------
 *
 * This function is capable of parsing the output of 'info source'.
 * It can get both the absolute and relative path to the source file.
 */
static void
commands_process_info_source(struct commands *c, char a)
{
    if (a == '\n') {
        /* parse gdbmi -file-list-exec-source-file output */
        mi_output *miout = mi_parse_gdb_output(ibuf_get(c->info_source_string));

        if (miout) {
            mi_results *res = (miout->type == MI_T_RESULT_RECORD) ? miout->c : NULL;

            while (res && (res->type == t_const)) {
                if (!strcmp(res->var, "file")) {
                    ibuf_clear(c->info_source_relative_path);
                    ibuf_add(c->info_source_relative_path, res->v.cstr);
                }
                else if (!strcmp(res->var, "fullname")) {
                    ibuf_clear(c->info_source_absolute_path);
                    ibuf_add(c->info_source_absolute_path, res->v.cstr);
                }
                else if (!strcmp(res->var, "line")) {
                    ibuf_clear(c->line_number);
                    ibuf_add(c->line_number, res->v.cstr);
                }

                res = res->next;
            }

            mi_free_output(miout);
        }

        c->info_source_ready = 1;
        ibuf_clear(c->info_source_string);
    } else {
        ibuf_addchar(c->info_source_string, a);
    }
}

static void mi_parse_sources(mi_output *miout, struct tgdb_list *source_files)
{
    if (miout) {
        mi_results *res = (miout->type == MI_T_RESULT_RECORD) ? miout->c : NULL;

        if (res && (res->type == t_list) && res->var && !strcmp(res->var, "files")) {
            res = res->v.rs;

            while (res && (res->type == t_tuple)) {
                mi_results *sub = res->v.rs;

                while (sub && sub->var) {
                    if ((sub->type == t_const) && !strcmp(sub->var, "fullname")) {
                        tgdb_list_append(source_files, sub->v.cstr);
                        sub->v.cstr = NULL;
                        break;
                    }

                    sub = sub->next;
                }

                res = res->next;
            }
        }
    }
}

/* process's source files */
static void commands_process_sources(struct commands *c, char a)
{
    if (a == '\n') {
        /* parse gdbmi -file-list-exec-source-files output */
        mi_output *miout = mi_parse_gdb_output(ibuf_get(c->info_sources_string));
        if (miout) {
            /* Add source files to our file list */
            mi_parse_sources(miout, c->inferior_source_files);

            mi_free_output(miout);
        }

        c->sources_ready = 1;
        ibuf_clear(c->info_sources_string);
    } else {
        ibuf_addchar(c->info_sources_string, a);
    }
}

//$ TODO: Document and put these in mi_gdb.h
extern "C" mi_bkpt *mi_get_bkpt(mi_results *p);

mi_results *mi_find_var(mi_results *res, const char *var, mi_val_type type)
{
    while (res) {
        if (res->var && (type == res->type) && !strcmp(res->var, var))
            return res;

        // if (res->type == t_const) res->v.cstr;
        if ((res->type == t_tuple) || (res->type == t_list)) {
            mi_results *t = mi_find_var(res->v.rs, var, type);
            if (t)
                return t;
        }

        res = res->next;
    }

    return NULL;
}

static void commands_process_breakpoints(struct commands *c, char a, struct tgdb_list *list)
{
    if (a == '\n') {
        /* parse gdbmi -break-info output */
        mi_output *miout = mi_parse_gdb_output(ibuf_get(c->breakpoint_string));

        if (miout && (miout->type == MI_T_RESULT_RECORD)) {
            mi_results *res = miout->c;
            mi_results *bplist = mi_find_var(res, "bkpt", t_tuple);

            while (bplist) {
                mi_bkpt *bkpt = mi_get_bkpt(bplist->v.rs);

                if (bkpt && bkpt->fullname) {
                    struct tgdb_breakpoint *tb = (struct tgdb_breakpoint *) cgdb_malloc(
                                sizeof (struct tgdb_breakpoint));
                    tb->funcname = bkpt->func;
                    tb->file = bkpt->fullname;
                    tb->line = bkpt->line;
                    tb->enabled = bkpt->enabled;

                    tgdb_list_append(c->breakpoint_list, tb);

                    bkpt->func = NULL;
                    bkpt->fullname = NULL;
                }

                mi_free_bkpt(bkpt);

                bplist = bplist->next;
            }


            if (tgdb_list_size(c->breakpoint_list)) {
                struct tgdb_response *response = (struct tgdb_response *)
                        cgdb_malloc(sizeof (struct tgdb_response));

                response->header = TGDB_UPDATE_BREAKPOINTS;
                response->choice.update_breakpoints.breakpoint_list =
                        c->breakpoint_list;

                /* At this point, annotate needs to send the breakpoints to the gui.
                 * All of the valid breakpoints are stored in breakpoint_queue. */
                tgdb_types_append_command(list, response);
            }
        }

        mi_free_output(miout);
        ibuf_clear(c->breakpoint_string);
    } else {
        ibuf_addchar(c->breakpoint_string, a);
    }
}

static void commands_process_completion(struct commands *c)
{
    const char *ptr = ibuf_get(c->tab_completion_string);
    const char *scomplete = "server complete ";

    /* Do not add the "server complete " matches, which is returned with 
     * GNAT 3.15p version of GDB. Most likely this could happen with other 
     * implementations that are derived from GDB.
     */
    if (strncmp(ptr, scomplete, strlen(scomplete)) != 0)
        tgdb_list_append(c->tab_completions, strdup(ptr));
}

/* process's source files */
static void commands_process_complete(struct commands *c, char a)
{
    ibuf_addchar(c->tab_completion_string, a);

    if (a == '\n') {
        ibuf_delchar(c->tab_completion_string); /* remove '\n' and null terminate */

        if (ibuf_length(c->tab_completion_string) > 0)
            commands_process_completion(c);

        ibuf_clear(c->tab_completion_string);
    }
}

void commands_free(struct commands *c, void *item)
{
    free((char *) item);
}

void commands_send_gui_sources(struct commands *c, struct tgdb_list *list)
{
    /* If the inferior program was not compiled with debug, then no sources
     * will be available. If no sources are available, do not return the
     * TGDB_UPDATE_SOURCE_FILES command. */
    if (tgdb_list_size(c->inferior_source_files) > 0) {
        struct tgdb_response *response = (struct tgdb_response *)
                cgdb_malloc(sizeof (struct tgdb_response));

        response->header = TGDB_UPDATE_SOURCE_FILES;
        response->choice.update_source_files.source_files =
                c->inferior_source_files;
        tgdb_types_append_command(list, response);
    }
}

void commands_send_gui_completions(struct commands *c, struct tgdb_list *list)
{
    /* If the inferior program was not compiled with debug, then no sources
     * will be available. If no sources are available, do not return the
     * TGDB_UPDATE_SOURCE_FILES command. */
/*  if (tgdb_list_size ( c->tab_completions ) > 0)*/
    struct tgdb_response *response = (struct tgdb_response *)
            cgdb_malloc(sizeof (struct tgdb_response));

    response->header = TGDB_UPDATE_COMPLETIONS;
    response->choice.update_completions.completion_list = c->tab_completions;
    tgdb_types_append_command(list, response);
}

void commands_process(struct commands *c, char a, struct tgdb_list *list)
{
    if (commands_get_state(c) == INFO_SOURCES) {
        commands_process_sources(c, a);
    } else if (commands_get_state(c) == INFO_BREAKPOINTS) {
        commands_process_breakpoints(c, a, list);
    } else if (commands_get_state(c) == COMPLETE) {
        commands_process_complete(c, a);
    } else if (commands_get_state(c) == INFO_LIST) {
        /* do nothing with data */
    } else if (commands_get_state(c) == INFO_SOURCE_FILENAME_PAIR
            || commands_get_state(c) == INFO_SOURCE_RELATIVE) {
        commands_process_info_source(c, a);
    }
}

/*******************************************************************************
 * This must be translated to just return the proper command.
 ******************************************************************************/

/* commands_prepare_info_breakpoints: 
 * ----------------------------------
 *  
 *  This prepares the command 'info breakpoints' 
 */
static void
commands_prepare_info_breakpoints(struct commands *c)
{
    ibuf_clear(c->breakpoint_string);
    commands_set_state(c, INFO_BREAKPOINTS, NULL);
}

/* commands_prepare_tab_completion:
 * --------------------------------
 *
 * This prepares the tab completion command
 */
static void
commands_prepare_tab_completion(struct annotate_two *a2, struct commands *c)
{
    c->tab_completion_ready = 0;
    ibuf_clear(c->tab_completion_string);
    commands_set_state(c, COMPLETE, NULL);
    global_set_start_completion(a2->g);
}

/* commands_prepare_info_sources: 
 * ------------------------------
 *
 *  This prepares the command 'info sources' by setting certain variables.
 */
static void
commands_prepare_info_sources(struct annotate_two *a2, struct commands *c)
{
    c->sources_ready = 0;
    ibuf_clear(c->info_sources_string);
    commands_set_state(c, INFO_SOURCES, NULL);
    global_set_start_info_sources(a2->g);
}

/* commands_prepare_list: 
 * -----------------------------
 *  This runs the command 'list filename:1' and then runs
 *  'info source' to find out what the absolute path to filename is.
 * 
 *    filename -> The name of the file to check the absolute path of.
 */
static void
commands_prepare_list(struct annotate_two *a2, struct commands *c,
        char *filename)
{
    commands_set_state(c, INFO_LIST, NULL);
    global_set_start_list(a2->g);
    c->info_source_ready = 0;
}

void commands_finalize_command(struct commands *c, struct tgdb_list *list)
{
    switch (commands_get_state(c)) {
        case INFO_SOURCE_RELATIVE:
        case INFO_SOURCE_FILENAME_PAIR:
            if (c->info_source_ready == 0) {
                struct tgdb_source_file *rejected = (struct tgdb_source_file *)
                        cgdb_malloc(sizeof (struct tgdb_source_file));
                struct tgdb_response *response = (struct tgdb_response *)
                        cgdb_malloc(sizeof (struct tgdb_response));

                if (c->last_info_source_requested == NULL)
                    rejected->absolute_path = NULL;
                else
                    rejected->absolute_path =
                            strdup(ibuf_get(c->last_info_source_requested));

                response->header = TGDB_ABSOLUTE_SOURCE_DENIED;
                response->choice.absolute_source_denied.source_file = rejected;
                tgdb_types_append_command(list, response);
            } else {
                if (commands_get_state(c) == INFO_SOURCE_RELATIVE)
                    commands_send_source_relative_source_file(c, list);
                else if (commands_get_state(c) == INFO_SOURCE_FILENAME_PAIR)
                    commands_send_source_absolute_source_file(c, list);
            }
            break;
        default:
            break;
    }
}

int
commands_prepare_for_command(struct annotate_two *a2,
        struct commands *c, struct tgdb_command *com)
{

    enum annotate_commands *a_com =
            (enum annotate_commands *) com->tgdb_client_private_data;

    /* Set the commands state to nothing */
    commands_set_state(c, VOID_COMMAND, NULL);

    /* The list command is no longer running */
    global_list_finished(a2->g);

    if (global_list_had_error(a2->g) == 1 && commands_get_state(c) == INFO_LIST) {
        global_set_list_error(a2->g, 0);
        return -1;
    }

    if (a_com == NULL) {
        data_set_state(a2, USER_COMMAND);
        return 0;
    }

    switch (*a_com) {
        case ANNOTATE_INFO_SOURCES:
            commands_prepare_info_sources(a2, c);
            break;
        case ANNOTATE_LIST:
            commands_prepare_list(a2, c, com->tgdb_command_data);
            break;
        case ANNOTATE_INFO_LINE:
            break;
        case ANNOTATE_INFO_SOURCE_RELATIVE:
            commands_prepare_info_source(a2, c, INFO_SOURCE_RELATIVE);
            break;
        case ANNOTATE_INFO_SOURCE_FILENAME_PAIR:
            commands_prepare_info_source(a2, c, INFO_SOURCE_FILENAME_PAIR);
            break;
        case ANNOTATE_INFO_BREAKPOINTS:
            commands_prepare_info_breakpoints(c);
            break;
        case ANNOTATE_TTY:
            break;              /* Nothing to do */
        case ANNOTATE_COMPLETE:
            commands_prepare_tab_completion(a2, c);
            io_debug_write_fmt("<%s\n>", com->tgdb_command_data);
            break;              /* Nothing to do */
        case ANNOTATE_INFO_SOURCE:
        case ANNOTATE_SET_PROMPT:
        case ANNOTATE_VOID:
            break;
        default:
            logger_write_pos(logger, __FILE__, __LINE__,
                    "commands_prepare_for_command error");
            break;
    };

    data_set_state(a2, INTERNAL_COMMAND);
    io_debug_write_fmt("<%s\n>", com->tgdb_command_data);

    return 0;
}

/** 
 * This is responsible for creating a command to run through the debugger.
 *
 * \param com 
 * The annotate command to run
 *
 * \param data 
 * Information that may be needed to create the command
 *
 * \return
 * A command ready to be run through the debugger or NULL on error.
 * The memory is malloc'd, and must be freed.
 */
static char *commands_create_command(struct commands *c,
        enum annotate_commands com, const char *data)
{
    char *ncom = NULL;

    switch (com) {
        case ANNOTATE_INFO_SOURCES:
            /* server info sources */
            ncom = strdup("server interp mi \"-file-list-exec-source-files\"\n");
            break;
        case ANNOTATE_LIST:
        {
            struct ibuf *temp_file_name = NULL;

            if (data != NULL) {
                temp_file_name = ibuf_init();
                ibuf_add(temp_file_name, data);
            }
            if (data == NULL)
                ncom = (char *) cgdb_malloc(sizeof (char) * (16));
            else
                ncom = (char *) cgdb_malloc(sizeof (char) * (18 +
                                strlen(data)));
            strcpy(ncom, "server list ");

            if (temp_file_name != NULL) {
                strcat(ncom, "\"");
                strcat(ncom, ibuf_get(temp_file_name));
                strcat(ncom, "\"");
                strcat(ncom, ":1");
            }

            /* This happens when the user wants to get the absolute path of 
             * the current file. They pass in NULL to represent that. */
            if (temp_file_name == NULL) {
                ibuf_free(c->last_info_source_requested);
                c->last_info_source_requested = NULL;
            } else {
                if (c->last_info_source_requested == NULL)
                    c->last_info_source_requested = ibuf_init();

                ibuf_clear(c->last_info_source_requested);
                ibuf_add(c->last_info_source_requested,
                        ibuf_get(temp_file_name));
            }

            strcat(ncom, "\n");

            ibuf_free(temp_file_name);
            temp_file_name = NULL;
            break;
        }
        case ANNOTATE_INFO_LINE:
            /* Not implemented? -symbol-info-line */
            ncom = strdup("server info line\n");
            break;
        case ANNOTATE_INFO_SOURCE_RELATIVE:
        case ANNOTATE_INFO_SOURCE_FILENAME_PAIR:
            /* server info source */
            ncom = strdup("server interp mi \"-file-list-exec-source-file\"\n");
            break;
        case ANNOTATE_INFO_BREAKPOINTS:
            /* server info breakpoints */
            ncom = strdup("server interp mi \"-break-info\"\n");
            break;
        case ANNOTATE_TTY:
        {
            struct ibuf *temp_tty_name = ibuf_init();

            ibuf_add(temp_tty_name, data);
            ncom = (char *) cgdb_malloc(sizeof (char) * (13 + strlen(data)));
            strcpy(ncom, "server tty ");
            strcat(ncom, ibuf_get(temp_tty_name));
            strcat(ncom, "\n");

            ibuf_free(temp_tty_name);
            temp_tty_name = NULL;
            break;
        }
        case ANNOTATE_COMPLETE:
            ncom = (char *) cgdb_malloc(sizeof (char) * (18 + strlen(data)));
            strcpy(ncom, "server complete ");
            strcat(ncom, data);
            strcat(ncom, "\n");
            break;
        case ANNOTATE_SET_PROMPT:
            ncom = strdup(data);
            break;
        case ANNOTATE_VOID:
        default:
            logger_write_pos(logger, __FILE__, __LINE__, "switch error");
            break;
    };

    return ncom;
}

int
commands_user_ran_command(struct commands *c,
        struct tgdb_list *client_command_list)
{
    if (commands_issue_command(c,
                    client_command_list,
                    ANNOTATE_INFO_BREAKPOINTS, NULL, 0) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }
#if 0
    /* This was added to allow support for TGDB to tell the FE when the user
     * switched locations due to a 'list foo:1' command. The info line would
     * get issued and the FE would know exactly what GDB was currently looking
     * at. However, it was noticed that the FE couldn't distinguish between when
     * a new file location should be displayed, or when a new file location 
     * shouldn't be displayed. For instance, if the user moves around in the
     * source window, and then types 'p argc' it would then get the original
     * position it was just at and the FE would show that spot again, but this
     * isn't necessarily what the FE wants.
     */
    if (commands_issue_command(c,
                    client_command_list, ANNOTATE_INFO_LINE, NULL, 0) == -1) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }
#endif

    return 0;
}

int
commands_issue_command(struct commands *c,
        struct tgdb_list *client_command_list,
        enum annotate_commands com, const char *data, int oob)
{
    char *ncom = commands_create_command(c, com, data);
    struct tgdb_command *client_command = NULL;
    enum annotate_commands *nacom =
            (enum annotate_commands *) cgdb_malloc(sizeof (enum
                    annotate_commands));

    *nacom = com;

    if (ncom == NULL) {
        logger_write_pos(logger, __FILE__, __LINE__,
                "commands_issue_command error");
        return -1;
    }

    /* This should send the command to tgdb-base to handle */
    if (oob == 0) {
        client_command = tgdb_command_create(ncom, TGDB_COMMAND_TGDB_CLIENT,
                (void *) nacom);
    } else if (oob == 1) {
        client_command = tgdb_command_create(ncom,
                TGDB_COMMAND_TGDB_CLIENT_PRIORITY, (void *) nacom);
    } else if (oob == 4) {
        client_command = tgdb_command_create(ncom, TGDB_COMMAND_TGDB_CLIENT,
                (void *) nacom);
    }

    if (ncom) {
        free(ncom);
        ncom = NULL;
    }

    /* Append to the command_container the commands */
    tgdb_list_append(client_command_list, client_command);

    return 0;
}
