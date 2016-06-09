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

    /*@} */

    /* info sources information {{{ */
    /*@{ */

  /** All of the sources.  */
    struct ibuf *info_sources_string;

  /** All of the source, parsed in put in a list, 1 at a time.  */
    struct tgdb_list *inferior_source_files;

    /*@} */
    /* }}} */

    /* tab completion information {{{ */
    /*@{ */

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
    c->info_sources_string = ibuf_init();
    c->inferior_source_files = tgdb_list_init();

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

/* commands_process_info_source:
 * -----------------------------
 *
 * This function is capable of parsing the output of 'info source'.
 * It can get both the absolute and relative path to the source file.
 */
static void
commands_process_info_source(struct commands *c, char a, struct tgdb_list *list)
{
    if (a == '\n') {
        /* parse gdbmi -file-list-exec-source-file output */
        mi_output *miout = mi_parse_gdb_output(ibuf_get(c->info_source_string));

        if (miout) {
            mi_results *res = (miout->type == MI_T_RESULT_RECORD) ? miout->c : NULL;
            int line = 0;
            char *fullname = NULL;
            char *file = NULL;

            while (res && (res->type == t_const)) {
                if (!strcmp(res->var, "file")) {
                    file = res->v.cstr;
                    res->v.cstr = NULL;
                }
                else if (!strcmp(res->var, "fullname")) {
                    fullname = res->v.cstr;
                    res->v.cstr = NULL;
                }
                else if (!strcmp(res->var, "line")) {
                    line = atoi(res->v.cstr);
                }

                res = res->next;
            }

            mi_free_output(miout);

            if (fullname) {
                struct tgdb_file_position *tfp = (struct tgdb_file_position *)
                        cgdb_malloc(sizeof (struct tgdb_file_position));
                struct tgdb_response *response = (struct tgdb_response *)
                        cgdb_malloc(sizeof (struct tgdb_response));

                tfp->absolute_path = fullname;
                tfp->relative_path = file;
                tfp->line_number = line;

                response->header = TGDB_UPDATE_FILE_POSITION;
                response->choice.update_file_position.file_position = tfp;

                tgdb_types_append_command(list, response);
            }

        }

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
static void commands_process_sources(struct commands *c, char a, struct tgdb_list *list)
{
    if (a == '\n') {
        /* parse gdbmi -file-list-exec-source-files output */
        mi_output *miout = mi_parse_gdb_output(ibuf_get(c->info_sources_string));
        if (miout) {
            struct tgdb_response *response;

            /* Add source files to our file list */
            mi_parse_sources(miout, c->inferior_source_files);
            mi_free_output(miout);

            response = (struct tgdb_response *) cgdb_malloc(sizeof (struct tgdb_response));
            response->header = TGDB_UPDATE_SOURCE_FILES;
            response->choice.update_source_files.source_files = c->inferior_source_files;
            tgdb_types_append_command(list, response);
        }

        ibuf_clear(c->info_sources_string);
    } else {
        ibuf_addchar(c->info_sources_string, a);
    }
}

//$ TODO mikesart: Document and put these in mi_gdb.h
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

/* process's command completion
    (gdb) server interp mi "complete com"
    &"complete com\n"
    ~"commands\n"
    ~"compare-sections\n"
    ~"compile\n"
    ~"complete\n"
    ^done
*/
static void commands_process_complete(struct commands *c, char a, struct tgdb_list *list)
{
    int len = ibuf_length(c->tab_completion_string);
    char *str = ibuf_get(c->tab_completion_string);

    if ((len > 5) && !strcmp(str + len - 5, "^done")) {
        struct tgdb_response *response;

        while (str) {
            char *end;
            mi_output *miout;

            /* Find end of line */
            end = strchr(str, '\n');
            if (!end)
                break;

            /* Zero terminate line and parse the gdbmi string */
            *end++ = 0;
            miout = mi_parse_gdb_output(str);

            /* If this is a console string, add it to our list */
            if (miout && (miout->sstype == MI_SST_CONSOLE)) {
                char *cstr = miout->c->v.cstr;
                size_t length = strlen(cstr);

                if (length > 0) {
                    /* Trim trailing newline */
                    if (cstr[length - 1] == '\n')
                        cstr[--length] = 0;

                    tgdb_list_append(c->tab_completions, miout->c->v.cstr);
                    miout->c->v.cstr = NULL;
                }
            }
            mi_free_output(miout);

            str = end;
        }

        response = (struct tgdb_response *)cgdb_malloc(sizeof (struct tgdb_response));
        response->header = TGDB_UPDATE_COMPLETIONS;
        response->choice.update_completions.completion_list = c->tab_completions;
        tgdb_types_append_command(list, response);

        ibuf_clear(c->tab_completion_string);
    } else {
        ibuf_addchar(c->tab_completion_string, a);
    }
}

void commands_process(struct commands *c, char a, struct tgdb_list *list)
{
    if (commands_get_state(c) == INFO_SOURCES) {
        commands_process_sources(c, a, list);
    } else if (commands_get_state(c) == INFO_BREAKPOINTS) {
        commands_process_breakpoints(c, a, list);
    } else if (commands_get_state(c) == COMMAND_COMPLETE) {
        commands_process_complete(c, a, list);
    } else if (commands_get_state(c) == INFO_SOURCE) {
        commands_process_info_source(c, a, list);
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

    if (a_com == NULL) {
        data_set_state(a2, USER_COMMAND);
        return 0;
    }

    switch (*a_com) {
        case ANNOTATE_INFO_SOURCES:
            ibuf_clear(c->info_sources_string);
            commands_set_state(c, INFO_SOURCES, NULL);
            break;
        case ANNOTATE_INFO_SOURCE:
            ibuf_clear(c->info_source_string);
            data_set_state(a2, INTERNAL_COMMAND);
            commands_set_state(c, INFO_SOURCE, NULL);
            break;
        case ANNOTATE_INFO_BREAKPOINTS:
            ibuf_clear(c->breakpoint_string);
            commands_set_state(c, INFO_BREAKPOINTS, NULL);
            break;
        case ANNOTATE_TTY:
            break;              /* Nothing to do */
        case ANNOTATE_COMPLETE:
            ibuf_clear(c->tab_completion_string);
            commands_set_state(c, COMMAND_COMPLETE, NULL);
            io_debug_write_fmt("<%s\n>", com->tgdb_command_data);
            break;              /* Nothing to do */
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
        case ANNOTATE_INFO_SOURCE:
            /* server info source */
            ncom = strdup("server interp mi \"-file-list-exec-source-file\"\n");
            break;
        case ANNOTATE_INFO_BREAKPOINTS:
            /* server info breakpoints */
            ncom = strdup("server interp mi \"-break-info\"\n");
            break;
        case ANNOTATE_TTY:
            /* server tty %s */
            ncom = sys_aprintf("server interp mi \"-inferior-tty-set %s\"\n", data);
            break;
        case ANNOTATE_COMPLETE:
            /* server complete */
            ncom = sys_aprintf("server interp mi \"complete %s\"\n", data);
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
