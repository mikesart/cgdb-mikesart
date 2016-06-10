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

  /** breakpoint information */
    /*@{ */

  /** The current breakpoint being parsed.  */
    struct ibuf *breakpoint_string;

  /** The disassemble string being parsed.  */
    struct ibuf *disassemble_string;

    /*@} */

  /** 'info source' information */

    /*@{ */

  /** The current info source line being parsed */
    struct ibuf *info_source_string;

  /** The current info frame line being parsed */
    struct ibuf *info_frame_string;

    /*@} */

    /* info sources information {{{ */
    /*@{ */

  /** All of the sources.  */
    struct ibuf *info_sources_string;

    /*@} */
    /* }}} */

    /* tab completion information {{{ */
    /*@{ */

  /** A tab completion item */
    struct ibuf *tab_completion_string;

    /*@} */
    /* }}} */
};

struct commands *commands_initialize(void)
{
    struct commands *c = (struct commands *) cgdb_malloc(sizeof (struct commands));

    c->cur_command_state = VOID_COMMAND;

    c->breakpoint_string = ibuf_init();
    c->disassemble_string = ibuf_init();

    c->info_source_string = ibuf_init();
    c->info_sources_string = ibuf_init();
    c->info_frame_string = ibuf_init();

    c->tab_completion_string = ibuf_init();
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

    ibuf_free(c->breakpoint_string);
    c->breakpoint_string = NULL;

    ibuf_free(c->disassemble_string);
    c->disassemble_string = NULL;

    ibuf_free(c->info_source_string);
    c->info_source_string = NULL;

    ibuf_free(c->info_sources_string);
    c->info_sources_string = NULL;

    ibuf_free(c->info_frame_string);
    c->info_frame_string = NULL;

    ibuf_free(c->tab_completion_string);
    c->tab_completion_string = NULL;

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

//$ TODO mikesart: Document and put these in mi_gdb.h
extern "C" mi_bkpt *mi_get_bkpt(mi_results *p);
extern "C" mi_asm_insns *mi_parse_insns(mi_results *c);

mi_results *mi_find_var(mi_results *res, const char *var, mi_val_type type)
{
    while (res) {
        if (res->var && (type == res->type) && !strcmp(res->var, var))
            return res;

        /* if (res->type == t_const) res->v.cstr; */
        if ((res->type == t_tuple) || (res->type == t_list)) {
            mi_results *t = mi_find_var(res->v.rs, var, type);
            if (t)
                return t;
        }

        res = res->next;
    }

    return NULL;
}

static int commands_parse_file_position( mi_results *res,
        struct tgdb_list *list)
{
    struct tgdb_file_position fp;

    memset(&fp, 0, sizeof(fp));

    while (res && (res->type == t_const)) {
        if (!strcmp(res->var, "fullname")) {
            fp.absolute_path = res->v.cstr;
            res->v.cstr = NULL;
        } else if (!strcmp(res->var, "line")) {
            fp.line_number = atoi(res->v.cstr);
        } else if (!strcmp(res->var, "addr")) {
            fp.addr = res->v.cstr;
            res->v.cstr = NULL;
        } else if (!strcmp(res->var, "from")) {
            fp.from = res->v.cstr;
            res->v.cstr = NULL;
        } else if (!strcmp(res->var, "func")) {
            fp.func = res->v.cstr;
            res->v.cstr = NULL;
        }

        res = res->next;
    }

    if (fp.absolute_path || fp.addr) {
        struct tgdb_file_position *tfp = (struct tgdb_file_position *)
                cgdb_malloc(sizeof (struct tgdb_file_position));
        struct tgdb_response *response = (struct tgdb_response *)
                cgdb_malloc(sizeof (struct tgdb_response));

        *tfp = fp;

        response->header = TGDB_UPDATE_FILE_POSITION;
        response->choice.update_file_position.file_position = tfp;

        tgdb_types_append_command(list, response);
        return 1;
    } else {
        free(fp.absolute_path);
        free(fp.addr);
        free(fp.from);
        free(fp.func);
    }

    return 0;
}

/* commands_process_info_frame:
 * -----------------------------
 *
 * This function is capable of parsing the output of 'info frame'.
 */
static void
commands_process_info_frame(struct annotate_two *a2, struct commands *c,
        char a, struct tgdb_list *list)
{
    /*
    ^error,msg="No registers."

    ~"\n\032\032frame-begin 0 0x400908\n"
    ~"\n\032\032frame-address\n"
    ~"\n\032\032frame-address-end\n"
    ~"\n\032\032frame-function-name\n"
    ~"\n\032\032frame-args\n"
    ~"\n\032\032frame-source-begin\n"
    ~"\n\032\032frame-source-file\n"
    ~"\n\032\032frame-source-file-end\n"
    ~"\n\032\032frame-source-line\n"
    ~"\n\032\032frame-source-end\n"
    ~"\n\032\032frame-end\n"
    ^done,frame={level="0",addr="0x0000000000400908",func="main",file="driver.cpp",fullname="/home/mikesart/dev/cgdb/cgdb-src/lib/util/driver.cpp",line="57"}

    post-prompt
    ~"\n\032\032frame-begin 0 0x7ffff6ecd7d0\n"
    ~"\n\032\032frame-address\n"
    ~"\n\032\032frame-address-end\n"
    ~"\n\032\032frame-function-name\n"
    ~"\n\032\032frame-args\n"
    ~"\n\032\032frame-where\n"
    ~"\n\032\032frame-end\n"
    ^done,frame={level="0",addr="0x00007ffff6ecd7d0",func="printf",from="/usr/lib/x86_64-linux-gnu/libasan.so.2"}
    */

    if (a == '\n') {
        char *str = ibuf_get(c->info_frame_string);

        /* Check for result record */
        if (*str == '^') {
            int success = 0;
            mi_output *miout = mi_parse_gdb_output(ibuf_get(c->info_frame_string));

            if (miout && (miout->tclass == MI_CL_DONE)) {
                mi_results *res = mi_find_var(miout->c, "frame", t_tuple);

                if (res) {
                    success = commands_parse_file_position(res->v.rs, list);
                }
            }

            if (!success) {
                /* We got nothing - try "info source" command. */
                commands_issue_command(a2->c, a2->client_command_list,
                                       ANNOTATE_INFO_SOURCE, NULL, 1);
            }

            mi_free_output(miout);
        }

        ibuf_clear(c->info_frame_string);
    } else {
        ibuf_addchar(c->info_frame_string, a);
    }
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

            commands_parse_file_position(res, list);

            mi_free_output(miout);
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
            struct tgdb_list *inferior_source_files = tgdb_list_init();

            /* Add source files to our file list */
            mi_parse_sources(miout, inferior_source_files);
            mi_free_output(miout);

            response = (struct tgdb_response *) cgdb_malloc(sizeof (struct tgdb_response));
            response->header = TGDB_UPDATE_SOURCE_FILES;
            response->choice.update_source_files.source_files = inferior_source_files;

            tgdb_types_append_command(list, response);
        }

        ibuf_clear(c->info_sources_string);
    } else {
        ibuf_addchar(c->info_sources_string, a);
    }
}

static void commands_process_breakpoints(struct commands *c, char a, struct tgdb_list *list)
{
    if (a == '\n') {
        /* parse gdbmi -break-info output */
        mi_output *miout = mi_parse_gdb_output(ibuf_get(c->breakpoint_string));

        if (miout && (miout->type == MI_T_RESULT_RECORD)) {
            struct tgdb_list *breakpoint_list = tgdb_list_init();
            mi_results *bplist = mi_find_var(miout->c, "bkpt", t_tuple);

            while (bplist) {
                mi_bkpt *bkpt = mi_get_bkpt(bplist->v.rs);

                if (bkpt && bkpt->fullname) {
                    struct tgdb_breakpoint *tb = (struct tgdb_breakpoint *) cgdb_malloc(
                                sizeof (struct tgdb_breakpoint));
                    tb->funcname = bkpt->func;
                    tb->file = bkpt->fullname;
                    tb->line = bkpt->line;
                    tb->enabled = bkpt->enabled;

                    tgdb_list_append(breakpoint_list, tb);

                    bkpt->func = NULL;
                    bkpt->fullname = NULL;
                }

                mi_free_bkpt(bkpt);

                bplist = bplist->next;
            }

            struct tgdb_response *response = (struct tgdb_response *)
                    cgdb_malloc(sizeof (struct tgdb_response));

            response->header = TGDB_UPDATE_BREAKPOINTS;
            response->choice.update_breakpoints.breakpoint_list =
                    breakpoint_list;

            /* At this point, annotate needs to send the breakpoints to the gui.
             * All of the valid breakpoints are stored in breakpoint_queue. */
            tgdb_types_append_command(list, response);
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
        struct tgdb_list *tab_completions = tgdb_list_init();

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

                    tgdb_list_append(tab_completions, miout->c->v.cstr);
                    miout->c->v.cstr = NULL;
                }
            }
            mi_free_output(miout);

            str = end;
        }

        response = (struct tgdb_response *)cgdb_malloc(sizeof (struct tgdb_response));
        response->header = TGDB_UPDATE_COMPLETIONS;
        response->choice.update_completions.completion_list = tab_completions;

        tgdb_types_append_command(list, response);

        ibuf_clear(c->tab_completion_string);
    } else {
        ibuf_addchar(c->tab_completion_string, a);
    }
}

static void
commands_process_disassemble(struct annotate_two *a2, struct commands *c,
                             char a, struct tgdb_list *list)
{
    /*
        interp mi "-data-disassemble -s $pc -e $pc+20 -- 0"
        ^done,asm_insns=[{address="0x0000000000400908",inst="mov    edi,0x400e40"},
            address="0x000000000040090d",inst="call   0x400760 <puts@plt>"},
            {address="0x0000000000400912",inst="mov    edi,0x400ea0"},
            {address="0x0000000000400917",inst="call   0x400760 <puts@plt>"}]
     */
    if (a == '\n') {
        /* parse gdbmi -data-disassemble command */
        mi_output *miout = mi_parse_gdb_output(ibuf_get(c->disassemble_string));

        if (miout && (miout->type == MI_T_RESULT_RECORD)) {
            mi_asm_insns *ins;
            mi_results *insns = mi_find_var(miout->c, "asm_insns", t_list);

            if (insns) {
                ins = mi_parse_insns(insns->v.rs);

                if (ins) {
                    mi_free_asm_insns(ins);
                }
            }
        }

        mi_free_output(miout);
        ibuf_clear(c->disassemble_string);
    } else {
        ibuf_addchar(c->disassemble_string, a);
    }
}

void commands_process(struct annotate_two *a2, struct commands *c, char a, struct tgdb_list *list)
{
    enum COMMAND_STATE state = commands_get_state(c);

    switch (state) {
        case VOID_COMMAND:
            break;
        case INFO_SOURCES:
            commands_process_sources(c, a, list);
            break;
        case INFO_BREAKPOINTS:
            commands_process_breakpoints(c, a, list);
            break;
        case COMMAND_COMPLETE:
            commands_process_complete(c, a, list);
            break;
        case INFO_SOURCE:
            commands_process_info_source(c, a, list);
            break;
        case INFO_FRAME:
            commands_process_info_frame(a2, c, a, list);
            break;
        case INFO_DISASSEMBLE:
            commands_process_disassemble(a2, c, a, list);
            break;
        }
}

int
commands_prepare_for_command(struct annotate_two *a2,
        struct commands *c, struct tgdb_command *com)
{
    int a_com = com->tgdb_client_private_data;

    /* Set the commands state to nothing */
    commands_set_state(c, VOID_COMMAND, NULL);

    if (a_com == -1) {
        data_set_state(a2, USER_COMMAND);
        return 0;
    }

    switch (a_com) {
        case ANNOTATE_INFO_SOURCES:
            ibuf_clear(c->info_sources_string);
            commands_set_state(c, INFO_SOURCES, NULL);
            break;
        case ANNOTATE_INFO_SOURCE:
            ibuf_clear(c->info_source_string);
            data_set_state(a2, INTERNAL_COMMAND);
            commands_set_state(c, INFO_SOURCE, NULL);
            break;
        case ANNOTATE_INFO_FRAME:
            ibuf_clear(c->info_frame_string);
            data_set_state(a2, INTERNAL_COMMAND);
            commands_set_state(c, INFO_FRAME, NULL);
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
        case ANNOTATE_DISASSEMBLE:
            ibuf_clear(c->disassemble_string);
            data_set_state(a2, INTERNAL_COMMAND);
            commands_set_state(c, INFO_DISASSEMBLE, NULL);
            break;
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
        case ANNOTATE_INFO_FRAME:
            /* server info frame */
            ncom = strdup("server interp mi \"-stack-info-frame\"\n");
            break;
        case ANNOTATE_DISASSEMBLE:
            /* x/20i $pc */
            if (!data)
                data = "-s $pc -e $pc+100 -- 0";
            ncom = sys_aprintf("server interp mi \"-data-disassemble %s\"\n", data);
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
commands_issue_command(struct commands *c,
        struct tgdb_list *client_command_list,
        enum annotate_commands com, const char *data, int oob)
{
    char *ncom = commands_create_command(c, com, data);
    struct tgdb_command *client_command = NULL;

    enum tgdb_command_choice choice = (oob == 1) ?
           TGDB_COMMAND_TGDB_CLIENT_PRIORITY : TGDB_COMMAND_TGDB_CLIENT;

    /* This should send the command to tgdb-base to handle */
    client_command = tgdb_command_create(ncom, choice, com);

    /* Append to the command_container the commands */
    tgdb_list_append(client_command_list, client_command);

    free(ncom);
    return 0;
}
