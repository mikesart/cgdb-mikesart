#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STDIO_H
#include <stdio.h>
#endif /* HAVE_STDIO_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#include "state_machine.h"
#include "annotate.h"
#include "commands.h"
#include "logger.h"
#include "data.h"
#include "globals.h"
#include "annotate_two.h"
#include "sys_util.h"
#include "ibuf.h"
#include "mi_gdb.h"

/* This package looks for annotations coming from gdb's output.
 * The program that is being debugged does not have its output pass
 * through here. So only gdb's output is filtered here.
 *
 * This is a simple state machine that is looking for annotations
 * in gdb's output. Annotations are of the form
 * '\n\032\032annotation\n'
 * However, on windows \n gets mapped to \r\n So we take account
 * for that by matching the form
 * '\r+\n\032\032annotation\r+\n'
 * 
 * When an annotation is found, this unit passes the annotation to the 
 * annotate unit and this unit is free of all responsibility :)
 */

enum state {
    SM_CGDB_GDBMI,
    SM_DATA,                       /* data from debugger */
    SM_NEW_LINE,                   /* got '\n' */
    SM_CONTROL_Z,                  /* got first ^Z '\032' */
    SM_ANNOTATION,                 /* got second ^Z '\032' */
    SM_NL_DATA                     /* got a nl at the end of annotation */
};

/**
 * The data needed to parse the output of GDB.
 */
struct state_machine {

    struct ibuf *cgdb_gdbmi_buffer;

    /**
	 * Annotations will be stored here.
	 */
    struct ibuf *tgdb_buffer;

    /**
	 * The state of the annotation parser ( current context ).
	 */
    enum state tgdb_state;
};

struct state_machine *state_machine_initialize(void)
{
    struct state_machine *sm =
            (struct state_machine *) cgdb_malloc(sizeof (struct state_machine));

    sm->cgdb_gdbmi_buffer = ibuf_init();
    sm->tgdb_buffer = ibuf_init();
    sm->tgdb_state = SM_DATA;

    return sm;
}

void state_machine_shutdown(struct state_machine *sm)
{
    ibuf_free(sm->cgdb_gdbmi_buffer);
    sm->cgdb_gdbmi_buffer = NULL;

    ibuf_free(sm->tgdb_buffer);
    sm->tgdb_buffer = NULL;

    free(sm);
}

int mi_get_result_record(struct ibuf *buf, char **lstart, int *id);

int a2_handle_data(struct annotate_two *a2, struct state_machine *sm,
        const char *data, const size_t size,
        char *gui_data, size_t * gui_size, struct tgdb_list *command_list)
{
    int i, counter = 0;

    /* track state to find next file and line number */
    for (i = 0; i < size; ++i) {
        /* Handle cgdb_gdbmi block */
        if (sm->tgdb_state == SM_CGDB_GDBMI) {
            if (data[i] != '\r')
                ibuf_addchar(sm->cgdb_gdbmi_buffer, data[i]);

            if (data[i] == '\n') {
                int id;
                char *result_line;
                int result_record = mi_get_result_record(sm->cgdb_gdbmi_buffer, &result_line, &id);

                if (result_record != -1) {
                    commands_process_cgdb_gdbmi(a2, sm->cgdb_gdbmi_buffer,
                            result_record, result_line, id, command_list);

                    sm->tgdb_state = SM_NL_DATA;
                    ibuf_clear(sm->cgdb_gdbmi_buffer);
                }
            }

            continue;
        }

        switch (data[i]) {
            /* Ignore all car returns outputted by gdb */
            case '\r':
                break;
            case '\n':
                switch (sm->tgdb_state) {
                case SM_DATA:
                    sm->tgdb_state = SM_NEW_LINE;
                    break;
                case SM_NEW_LINE:
                    sm->tgdb_state = SM_NEW_LINE;
                    data_process(a2, '\n', gui_data, &counter, command_list);
                    break;
                case SM_CONTROL_Z:
                    sm->tgdb_state = SM_DATA;
                    data_process(a2, '\n', gui_data, &counter, command_list);
                    data_process(a2, '\032', gui_data, &counter, command_list);
                    break;
                case SM_ANNOTATION: {
                    /* Found an annotation */
                    int id;
                    int is_cgdb_gdbmi;

                    sm->tgdb_state = SM_NL_DATA;
                    id = tgdb_parse_annotation(a2, ibuf_get(sm->tgdb_buffer),
                            ibuf_length(sm->tgdb_buffer), command_list,
                            &is_cgdb_gdbmi);

                    if (is_cgdb_gdbmi) {

                        ibuf_adddata(sm->cgdb_gdbmi_buffer,
                                     ibuf_get(sm->tgdb_buffer), ibuf_length(sm->tgdb_buffer));
                        sm->tgdb_state = SM_CGDB_GDBMI;
                    }

                    ibuf_clear(sm->tgdb_buffer);
                    break;
                    }
                case SM_NL_DATA:
                    sm->tgdb_state = SM_NEW_LINE;
                    break;
                default:
                    logger_write_pos(logger, __FILE__, __LINE__,
                            "Bad state transition");
                    break;
                }               /* end switch */
                break;

            case '\032':
                switch (sm->tgdb_state) {
                    case SM_DATA:
                        sm->tgdb_state = SM_DATA;
                        data_process(a2, '\032', gui_data, &counter,
                                command_list);
                        break;
                    case SM_NEW_LINE:
                        sm->tgdb_state = SM_CONTROL_Z;
                        break;
                    case SM_NL_DATA:
                        sm->tgdb_state = SM_CONTROL_Z;
                        break;
                    case SM_CONTROL_Z:
                        sm->tgdb_state = SM_ANNOTATION;
                        break;
                    case SM_ANNOTATION:
                        ibuf_addchar(sm->tgdb_buffer, data[i]);
                        break;
                    default:
                        logger_write_pos(logger, __FILE__, __LINE__,
                                "Bad state transition");
                        break;
                }               /* end switch */
                break;
            default:
                switch (sm->tgdb_state) {
                    case SM_DATA:
                        data_process(a2, data[i], gui_data, &counter,
                                command_list);
                        break;
                    case SM_NL_DATA:
                        sm->tgdb_state = SM_DATA;
                        data_process(a2, data[i], gui_data, &counter,
                                command_list);
                        break;
                    case SM_NEW_LINE:
                        sm->tgdb_state = SM_DATA;
                        data_process(a2, '\n', gui_data, &counter,
                                command_list);
                        data_process(a2, data[i], gui_data, &counter,
                                command_list);
                        break;
                    case SM_CONTROL_Z:
                        sm->tgdb_state = SM_DATA;
                        data_process(a2, '\n', gui_data, &counter,
                                command_list);
                        data_process(a2, '\032', gui_data, &counter,
                                command_list);
                        data_process(a2, data[i], gui_data, &counter,
                                command_list);
                        break;
                    case SM_ANNOTATION: {
                        int end;
                        for (end = i + 1; end < size; end++) {
                            if ((data[end] == '\r') || (data[end] == '\n') || (data[end] == '\032')) {
                                break;
                            }
                        }

                        ibuf_adddata(sm->tgdb_buffer, data + i, end - i);
                        i = end - 1;
                        break;
                    }
                    default:
                        logger_write_pos(logger, __FILE__, __LINE__,
                                "Bad state transition");
                        break;
                }               /* end switch */
                break;
        }                       /* end switch */
    }                           /* end for */

    gui_data[counter] = '\0';
    *gui_size = counter;
    return 0;
}
