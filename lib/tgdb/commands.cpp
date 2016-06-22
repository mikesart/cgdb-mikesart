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

#if HAVE_CTYPE_H
#include <ctype.h>
#endif

/* Local includes */
#include "commands.h"
#include "io.h"
#include "tgdb_types.h"
#include "logger.h"
#include "sys_util.h"
#include "queue.h"
#include "ibuf.h"
#include "a2-tgdb.h"
#include "queue.h"
#include "tgdb_list.h"
#include "annotate_two.h"
#include "mi_gdb.h"

static int gdb_version_major = 0;
static int gdb_version_minor = 0;

int free_breakpoint(void *item)
{
    struct tgdb_breakpoint *bp = (struct tgdb_breakpoint *)item;

    if (bp->file)
    {
        free(bp->file);
        bp->file = NULL;
    }

    if (bp->funcname)
    {
        free(bp->funcname);
        bp->funcname = NULL;
    }

    free(bp);
    bp = NULL;

    return 0;
}

int free_char_star(void *item)
{
    char *s = (char *)item;

    free(s);
    s = NULL;

    return 0;
}

//$ TODO mikesart: Document and put these in mi_gdb.h
extern "C" mi_bkpt *mi_get_bkpt(mi_results *p);
extern "C" mi_asm_insns *mi_parse_insns(mi_results *c);

mi_results *mi_find_var(mi_results *res, const char *var, mi_val_type type)
{
    while (res)
    {
        if (res->var && (type == res->type) && !strcmp(res->var, var))
            return res;

        /* if (res->type == t_const) res->v.cstr; */
        if ((res->type == t_tuple) || (res->type == t_list))
        {
            mi_results *t = mi_find_var(res->v.rs, var, type);
            if (t)
                return t;
        }

        res = res->next;
    }

    return NULL;
}

int mi_get_result_record(struct ibuf *buf, char **lstart, int *id)
{
    int pos;
    int len = ibuf_length(buf);
    char *line = ibuf_get(buf);

    *id = -1;

    /* Find start of this line */
    for (pos = len - 2; pos > 0; pos--)
    {
        if (line[pos] == '\n')
        {
            line += pos + 1;
            break;
        }
    }

    *lstart = line;

    /* Skip over result class id */
    if (isdigit(*line))
    {
        *id = atoi(line);

        while (isdigit(*line))
            line++;
    }

    /* Check for result classes */
    if (*line++ == '^')
    {
        if (!strncmp(line, "done", 4))
            return MI_CL_DONE;
        else if (!strncmp(line, "running", 7))
            return MI_CL_RUNNING;
        else if (!strncmp(line, "connected", 9))
            return MI_CL_CONNECTED;
        else if (!strncmp(line, "error", 5))
            return MI_CL_ERROR;
        else if (!strncmp(line, "exit", 4))
            return MI_CL_EXIT;
    }

    return -1;
}

static int commands_parse_file_position(int id, mi_results *res,
    struct tgdb_list *list)
{
    struct tgdb_file_position fp;

    memset(&fp, 0, sizeof(fp));

    while (res && (res->type == t_const))
    {
        if (!strcmp(res->var, "fullname"))
        {
            fp.absolute_path = res->v.cstr;
            res->v.cstr = NULL;
        }
        else if (!strcmp(res->var, "line"))
        {
            fp.line_number = atoi(res->v.cstr);
        }
        else if (!strcmp(res->var, "addr"))
        {
            fp.addr = sys_hexstr_to_u64(res->v.cstr);
        }
        else if (!strcmp(res->var, "from"))
        {
            fp.from = res->v.cstr;
            res->v.cstr = NULL;
        }
        else if (!strcmp(res->var, "func"))
        {
            fp.func = res->v.cstr;
            res->v.cstr = NULL;
        }

        res = res->next;
    }

    if (fp.absolute_path || fp.addr)
    {
        struct tgdb_file_position *tfp = (struct tgdb_file_position *)
            cgdb_malloc(sizeof(struct tgdb_file_position));
        struct tgdb_response *response = (struct tgdb_response *)
            cgdb_malloc(sizeof(struct tgdb_response));

        *tfp = fp;

        response->result_id = id;
        response->request = NULL;
        response->header = TGDB_UPDATE_FILE_POSITION;
        response->choice.update_file_position.file_position = tfp;

        tgdb_types_append_command(list, response);
        return 1;
    }
    else
    {
        free(fp.absolute_path);
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
commands_process_info_frame(struct annotate_two *a2, struct ibuf *buf,
    int result_record, char *result_line, int id,
    struct tgdb_list *list)
{
    /*
    ^error,msg="No registers."

    ~"\n\032\032frame-begin 0 0x400908\n"
    ...
    ~"\n\032\032frame-end\n"
    ^done,frame={level="0",addr="0x0000000000400908",func="main",file="driver.cpp",fullname="/home/mikesart/dev/cgdb/cgdb-src/lib/util/driver.cpp",line="57"}

    post-prompt
    ~"\n\032\032frame-begin 0 0x7ffff6ecd7d0\n"
    ...
    ~"\n\032\032frame-end\n"
    ^done,frame={level="0",addr="0x00007ffff6ecd7d0",func="printf",from="/usr/lib/x86_64-linux-gnu/libasan.so.2"}
    */

    int success = 0;
    mi_output *miout = mi_parse_gdb_output(result_line, NULL);

    if (miout && (miout->tclass == MI_CL_DONE))
    {
        mi_results *res = mi_find_var(miout->c, "frame", t_tuple);

        if (res)
        {
            success = commands_parse_file_position(id, res->v.rs, list);
        }
    }

    if (!success)
    {
        /* We got nothing - try "info source" command. */
        commands_issue_command(a2->client_command_list,
            ANNOTATE_INFO_SOURCE, NULL, 1, NULL);
    }

    mi_free_output(miout);
}

/* commands_process_info_source:
 * -----------------------------
 *
 * This function is capable of parsing the output of 'info source'.
 * It can get both the absolute and relative path to the source file.
 */
static void
commands_process_info_source(struct annotate_two *a2, struct ibuf *buf,
    int result_record, char *result_line, int id,
    struct tgdb_list *list)
{
    /* parse gdbmi -file-list-exec-source-file output:
       ^done,line="51",file="driver.cpp",
        fullname="/home/mikesart/dev/cgdb/cgdb-src/lib/util/driver.cpp",
        macro-info="0"
     */
    mi_output *miout = mi_parse_gdb_output(result_line, NULL);

    if (miout)
    {
        mi_results *res = (miout->type == MI_T_RESULT_RECORD) ? miout->c : NULL;

        commands_parse_file_position(id, res, list);

        mi_free_output(miout);
    }
}

static char **
mi_parse_sources(mi_output *miout)
{
    char **source_files = NULL;

    if (miout)
    {
        mi_results *res = (miout->type == MI_T_RESULT_RECORD) ? miout->c : NULL;

        if (res && (res->type == t_list) && res->var && !strcmp(res->var, "files"))
        {
            res = res->v.rs;

            while (res && (res->type == t_tuple))
            {
                mi_results *sub = res->v.rs;

                while (sub && sub->var)
                {
                    if ((sub->type == t_const) && !strcmp(sub->var, "fullname"))
                    {
                        sbpush(source_files, sub->v.cstr);
                        sub->v.cstr = NULL;
                        break;
                    }

                    sub = sub->next;
                }

                res = res->next;
            }
        }
    }

    return source_files;
}

static void
commands_process_gdbversion(struct annotate_two *a2, struct ibuf *buf,
    int record_result, char *result_line, int id,
    struct tgdb_list *list)
{
    char *str = ibuf_get(buf);

    /*
        (gdb) interp mi "-gdb-version"
        ~"GNU gdb (Debian 7.10-1.1) 7.10\n"
        ~"Copyright (C) 2015 Free Software Foundation, Inc.\n"
        ~"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\nThis is free software: you are free to change and redistribute it.\nThere is NO WARRANTY, to the extent permitted by law.  Type \"show copying\"\nand \"show warranty\" for details.\n"
        ~"This GDB was configured as \"x86_64-linux-gnu\".\nType \"show configuration\" for configuration details."
        ~"\nFor bug reporting instructions, please see:\n"
        ~"<http://www.gnu.org/software/gdb/bugs/>.\n"
        ~"Find the GDB manual and other documentation resources online at:\n<http://www.gnu.org/software/gdb/documentation/>.\n"
        ~"For help, type \"help\".\n"
        ~"Type \"apropos word\" to search for commands related to \"word\".\n"
        ^done
     */
    while (str && !gdb_version_major)
    {
        char *end;
        mi_output *miout;

        /* Find end of line */
        end = strchr(str, '\n');
        if (!end)
            break;

        /* Zero terminate line and parse the gdbmi string */
        *end++ = 0;
        miout = mi_parse_gdb_output(str, NULL);

        /* If this is a console string, add it to our list */
        if (miout && (miout->sstype == MI_SST_CONSOLE))
        {
            char *version = strrchr(miout->c->v.cstr, ' ');

            if (version)
            {
                gdb_version_major = atoi(version + 1);

                version = strchr(version, '.');

                if (version)
                    gdb_version_minor = atoi(version + 1);
            }
        }
        mi_free_output(miout);

        str = end;
    }
}

/* process's source files */
static void
commands_process_sources(struct annotate_two *a2, struct ibuf *buf,
    int record_result, char *result_line, int id,
    struct tgdb_list *list)
{
    /* parse gdbmi -file-list-exec-source-files output */
    mi_output *miout = mi_parse_gdb_output(result_line, NULL);

    if (miout)
    {
        char **source_files;
        struct tgdb_response *response;

        /* Add source files to our file list */
        source_files = mi_parse_sources(miout);

        response = (struct tgdb_response *)cgdb_malloc(sizeof(struct tgdb_response));
        response->result_id = id;
        response->request = NULL;
        response->header = TGDB_UPDATE_SOURCE_FILES;
        response->choice.update_source_files.source_files = source_files;

        tgdb_types_append_command(list, response);

        mi_free_output(miout);
    }
}

static void
commands_process_breakpoints(struct annotate_two *a2, struct ibuf *buf,
    int result_record, char *result_line, int id,
    struct tgdb_list *list)
{
    mi_output *miout = mi_parse_gdb_output(result_line, NULL);

    if (miout && (miout->type == MI_T_RESULT_RECORD))
    {
        struct tgdb_breakpoint *breakpoints = NULL;
        mi_results *bplist = mi_find_var(miout->c, "bkpt", t_tuple);

        while (bplist)
        {
            mi_bkpt *bkpt = mi_get_bkpt(bplist->v.rs);

            if (bkpt && bkpt->fullname)
            {
                struct tgdb_breakpoint tb;

                tb.funcname = bkpt->func;
                tb.file = bkpt->fullname;
                tb.line = bkpt->line;
                tb.enabled = bkpt->enabled;
                sbpush(breakpoints, tb);

                bkpt->func = NULL;
                bkpt->fullname = NULL;
            }

            mi_free_bkpt(bkpt);

            bplist = bplist->next;
        }

        struct tgdb_response *response = (struct tgdb_response *)
            cgdb_malloc(sizeof(struct tgdb_response));

        response->result_id = id;
        response->request = NULL;
        response->header = TGDB_UPDATE_BREAKPOINTS;
        response->choice.update_breakpoints.breakpoints = breakpoints;

        /* At this point, annotate needs to send the breakpoints to the gui.
            All of the valid breakpoints are stored in breakpoint_queue. */
        tgdb_types_append_command(list, response);
    }

    mi_free_output(miout);
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
static void
commands_process_complete(struct annotate_two *a2, struct ibuf *buf,
    int result_record, char *result_line, int id,
    struct tgdb_list *list)
{
    char *str = ibuf_get(buf);
    struct tgdb_response *response;
    struct tgdb_list *tab_completions = tgdb_list_init();

    while (str)
    {
        char *end;
        mi_output *miout;

        /* Find end of line */
        end = strchr(str, '\n');
        if (!end)
            break;

        /* Zero terminate line and parse the gdbmi string */
        *end++ = 0;
        miout = mi_parse_gdb_output(str, NULL);

        /* If this is a console string, add it to our list */
        if (miout && (miout->sstype == MI_SST_CONSOLE))
        {
            char *cstr = miout->c->v.cstr;
            size_t length = strlen(cstr);

            if (length > 0)
            {
                char *cr = strchr(cstr, '\r');
                if (cr)
                    *cr = 0;
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

    response = (struct tgdb_response *)cgdb_malloc(sizeof(struct tgdb_response));
    response->result_id = id;
    response->request = NULL;
    response->header = TGDB_UPDATE_COMPLETIONS;
    response->choice.update_completions.completion_list = tab_completions;
    tgdb_types_append_command(list, response);
}

/*
    &"disassemble /s\n"
    ~"Dump of assembler code for function main(int, char**):\n"
    ~"driver.cpp:\n"
    ~"54\t{\n"
    ~"   0x00000000004008f6 <+0>:\tpush   rbp\n"
    ~"   0x00000000004008f7 <+1>:\tmov    rbp,rsp\n"
    ~"   0x00000000004008fa:\tpush   r12\n"
    ~"   0x00000000004008fc:\tpush   rbx\n"
    ~"   0x00000000004008fd:\tsub    rsp,0x30\n"
    ~"   0x0000000000400901:\tmov    DWORD PTR [rbp-0x34],edi\n"
    ~"   0x0000000000400904:\tmov    QWORD PTR [rbp-0x40],rsi\n"
    ~"\n"
    ~"55\t    int fg, bg, bold;\n"
    ~"56\t\n"
    ~"57\t    printf( \"\\n\\033[1;33m --- Terminal Color Chart ---\\033[0m\\n\\n\" );\n"
    ~"=> 0x0000000000400908:\tmov    edi,0x400e40\n"
    ~"   0x000000000040090d:\tcall   0x400760 <puts@plt>\n"
    ~"\n"
    ...
    ~"   0x0000000000400d66:\tadd    rsp,0x30\n"
    ~"   0x0000000000400d6a:\tpop    rbx\n"
    ~"   0x0000000000400d6b:\tpop    r12\n"
    ~"   0x0000000000400d6d:\tpop    rbp\n"
    ~"   0x0000000000400d6e:\tret    \n"
    ~"End of assembler dump.\n"
    ^done
 */
static uint64_t disassemble_parse_address(const char *line)
{
    char *end;
    uint64_t val = 0;

    while (isspace(*line))
        line++;

    if (line[0] == '0' && line[1] == 'x')
        val = strtoull(line, &end, 16);

    return val;
}

static void
commands_process_disassemble_func(struct annotate_two *a2, struct ibuf *buf,
    int result_record, char *result_line, int id,
    struct tgdb_list *list)

{
    char **disasm = NULL;
    uint64_t addr_start = 0;
    uint64_t addr_end = 0;
    struct tgdb_response *response;

    if (result_record == MI_CL_ERROR)
    {
        mi_output *miout = mi_parse_gdb_output(result_line, NULL);

        /* Grab the error message */
        sbpush(disasm, miout->c->v.cstr);
        miout->c->v.cstr = NULL;

        mi_free_output(miout);
    }
    else
    {
        char *str = ibuf_get(buf);

        while (str)
        {
            char *end;
            mi_output *miout;

            /* Find end of line */
            end = strchr(str, '\n');
            if (!end)
                break;

            /* Zero terminate line and parse the gdbmi string */
            *end++ = 0;
            if (!end[0])
                continue;

            miout = mi_parse_gdb_output(str, NULL);

            /* If this is a console string, add it to our list */
            if (miout && (miout->sstype == MI_SST_CONSOLE))
            {
                char *cstr = miout->c->v.cstr;
                size_t length = strlen(cstr);

                if (length > 0)
                {
                    uint64_t addr;

                    /* Trim trailing newline */
                    if (cstr[length - 1] == '\n')
                        cstr[--length] = 0;

                    /* Trim the gdb current location pointer off */
                    if (cstr[0] == '=' && cstr[1] == '>')
                    {
                        cstr[0] = ' ';
                        cstr[1] = ' ';
                    }

                    addr = sys_hexstr_to_u64(cstr);
                    if (addr)
                    {
                        addr_start = addr_start ? MIN(addr, addr_start) : addr;
                        addr_end = MAX(addr, addr_end);
                    }
                    sbpush(disasm, miout->c->v.cstr);
                    miout->c->v.cstr = NULL;
                }
            }
            mi_free_output(miout);

            str = end;
        }
    }

    response = (struct tgdb_response *)cgdb_malloc(sizeof(struct tgdb_response));
    response->result_id = id;
    response->request = NULL;
    response->header = TGDB_DISASSEMBLE_FUNC;
    response->choice.disassemble.error = (result_record == MI_CL_ERROR);
    response->choice.disassemble.disasm = disasm;
    response->choice.disassemble.addr_start = addr_start;
    response->choice.disassemble.addr_end = addr_end;
    tgdb_types_append_command(list, response);
}

void commands_process_cgdb_gdbmi(struct annotate_two *a2, struct ibuf *buf,
    int result_record, char *result_line, int id,
    struct tgdb_list *list)
{
    const char *state = strchr(ibuf_get(buf), ':');

    if (!state)
    {
        logger_write_pos(logger, __FILE__, __LINE__,
            "commands_process_cgdb_gdbmi state error");
        return;
    }

    state++;
    if (!strncmp(state, "info_sources", 12))
        commands_process_sources(a2, buf, result_record, result_line, id, list);
    else if (!strncmp(state, "info_source", 11))
        commands_process_info_source(a2, buf, result_record, result_line, id, list);
    else if (!strncmp(state, "info_frame", 10))
        commands_process_info_frame(a2, buf, result_record, result_line, id, list);
    else if (!strncmp(state, "info_disassemble", 16))
        commands_process_disassemble_func(a2, buf, result_record, result_line, id, list);
    else if (!strncmp(state, "info_disassemble_func", 21))
        commands_process_disassemble_func(a2, buf, result_record, result_line, id, list);
    else if (!strncmp(state, "info_breakpoints", 16))
        commands_process_breakpoints(a2, buf, result_record, result_line, id, list);
    else if (!strncmp(state, "info_complete", 13))
        commands_process_complete(a2, buf, result_record, result_line, id, list);
    else if (!strncmp(state, "gdb_version", 11))
        commands_process_gdbversion(a2, buf, result_record, result_line, id, list);
    else if (!strncmp(state, "info_tty", 8))
        ;
    else
        logger_write_pos(logger, __FILE__, __LINE__,
            "commands_process_cgdb_gdbmi error");
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
static char *commands_create_command(enum annotate_commands com,
    const char *data, int command_id)
{
    char *cmd = NULL;
    const char *name = NULL;

    switch (com)
    {
    case ANNOTATE_GDB_VERSION:
        name = "gdb_version";
        cmd = strdup("-gdb-version");
        break;
    case ANNOTATE_INFO_SOURCES:
        /* server info sources */
        name = "info_sources";
        cmd = strdup("-file-list-exec-source-files");
        break;
    case ANNOTATE_INFO_SOURCE:
        /* server info source */
        name = "info_source";
        cmd = strdup("-file-list-exec-source-file");
        break;
    case ANNOTATE_INFO_FRAME:
        /* server info frame */
        name = "info_frame";
        cmd = strdup("-stack-info-frame");
        break;
    case ANNOTATE_DISASSEMBLE:
        /* x/20i $pc */
        name = "info_disassemble";
        cmd = sys_aprintf("x/%s", data);
        break;
    case ANNOTATE_DISASSEMBLE_FUNC:
        name = "info_disassemble_func";
        cmd = sys_aprintf("disassemble %s", data ? data : "");
        break;
    case ANNOTATE_INFO_BREAKPOINTS:
        /* server info breakpoints */
        name = "info_breakpoints";
        cmd = strdup("-break-info");
        break;
    case ANNOTATE_TTY:
        /* server tty %s */
        name = "info_tty";
        cmd = sys_aprintf("-inferior-tty-set %s", data);
        break;
    case ANNOTATE_COMPLETE:
        /* server complete */
        name = "info_complete";
        cmd = sys_aprintf("complete %s", data);
        break;

    case ANNOTATE_VOID:
    default:
        logger_write_pos(logger, __FILE__, __LINE__, "switch error");
        break;
    };

    if (cmd)
    {
        /* Add our gdbmi command with the cgdb-gdbmi pre-command annotation */
        char *fullcmd = sys_aprintf("server echo \\n\\032\\032cgdb-gdbmi%d:%s\n"
                                    "server interp mi \"%d%s\"\n",
            command_id, name, command_id, cmd);
        free(cmd);
        return fullcmd;
    }

    return NULL;
}

void tgdb_command_destroy(void *item)
{
    struct tgdb_command *tc = (struct tgdb_command *)item;

    free(tc->tgdb_command_data);
    free(tc);
}

int tgdb_get_gdb_version(int *major, int *minor)
{
    *major = gdb_version_major;
    *minor = gdb_version_minor;

    return (gdb_version_major > 0);
}

static int command_get_next_id()
{
    static int command_id = 100;

    return command_id++;
}

int commands_issue_command(struct tgdb_list *client_command_list,
    enum annotate_commands com, const char *data, int oob, int *id)
{
    struct tgdb_command *tc;
    int command_id = command_get_next_id();
    char *command = commands_create_command(com, data, command_id);

    enum tgdb_command_choice choice = (oob == 1) ?
        TGDB_COMMAND_TGDB_CLIENT_PRIORITY :
        TGDB_COMMAND_TGDB_CLIENT;

    tc = (struct tgdb_command *)cgdb_malloc(sizeof(struct tgdb_command));
    tc->command_choice = choice;
    tc->tgdb_client_private_data = com;
    tc->tgdb_command_data = command;

    /* Append to the command_container the commands */
    tgdb_list_append(client_command_list, tc);

    if (id)
        *id = command_id;
    return 0;
}
