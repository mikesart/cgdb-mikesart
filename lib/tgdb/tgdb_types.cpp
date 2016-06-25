#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#if HAVE_STDIO_H
#include <stdio.h>
#endif

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

/* Local includes */
#include "tgdb.h"
#include "tgdb_types.h"
#include "sys_util.h"
#include "tgdb_list.h"

static int tgdb_types_source_files_free(void *data)
{
    char *s = (char *)data;

    free(s);
    s = NULL;
    return 0;
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
        struct tgdb_list *list =
            com->choice.update_completions.completion_list;

        tgdb_list_free(list, tgdb_types_source_files_free);
        tgdb_list_destroy(list);

        com->choice.update_completions.completion_list = NULL;
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
