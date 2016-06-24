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
#endif

#include "tgdb_types.h"
#include "tgdb_command.h"
#include "sys_util.h"

struct tgdb_command *tgdb_command_create(const char *tgdb_command_data,
        enum tgdb_command_choice command_choice, int client_data)
{
    struct tgdb_command *tc;

    tc = (struct tgdb_command *) cgdb_malloc(sizeof (struct tgdb_command));

    tc->command_choice = command_choice;
    tc->tgdb_client_private_data = client_data;
    tc->tgdb_command_data = tgdb_command_data ? strdup(tgdb_command_data) : NULL;

    return tc;
}

void tgdb_command_destroy(void *item)
{
    struct tgdb_command *tc = (struct tgdb_command *) item;

    free(tc->tgdb_command_data);
    free(tc);
}
