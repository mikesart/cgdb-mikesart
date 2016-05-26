/* highlight.c:
 * ------------
 * 
 * Syntax highlighting routines.
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* System Includes */
#if HAVE_CTYPE_H
#include <ctype.h>
#endif /* HAVE_CTYPE_H */

#if HAVE_LIMITS_H
#include <limits.h>             /* CHAR_MAX */
#endif /* HAVE_LIMITS_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#if HAVE_REGEX_H
#include <regex.h>
#endif /* HAVE_REGEX_H */

/* Local Includes */
#include "cgdb.h"
#include "highlight.h"
#include "highlight_groups.h"
#include "sources.h"
#include "tokenizer.h"
#include "sys_util.h"
#include "interface.h"
#include "logger.h"

/* ----------- */
/* Definitions */
/* ----------- */

#define HL_CHAR CHAR_MAX        /* Special marker character */

/* --------------- */
/* Local Variables */
/* --------------- */

static void add_type(struct ibuf *ibuf, int *lasttype, int newtype)
{
    /* Add HL_CHAR + HLG_* token only if the HLG_ type has changed */
    if ( *lasttype != newtype ) {
        ibuf_addchar(ibuf, HL_CHAR);
        ibuf_addchar(ibuf, newtype);
        *lasttype = newtype;
    }
}

/* Returns HLG_LAST on error */
static enum hl_group_kind hlg_from_tokenizer_type(enum tokenizer_type type, const char *tok_data)
{
    switch(type) {
        case TOKENIZER_COLOR: return hl_get_color_group(hl_groups_instance, tok_data);
        case TOKENIZER_KEYWORD: return HLG_KEYWORD;
        case TOKENIZER_TYPE: return HLG_TYPE;
        case TOKENIZER_LITERAL: return HLG_LITERAL;
        case TOKENIZER_NUMBER: return HLG_TEXT;
        case TOKENIZER_COMMENT: return HLG_COMMENT;
        case TOKENIZER_DIRECTIVE: return HLG_DIRECTIVE;
        case TOKENIZER_TEXT: return HLG_TEXT;
        case TOKENIZER_ERROR: return HLG_TEXT;
        case TOKENIZER_SEARCH: return HLG_SEARCH;
        case TOKENIZER_STATUS_BAR: return HLG_STATUS_BAR;
        case TOKENIZER_ARROW: return HLG_ARROW;
        case TOKENIZER_LINE_HIGHLIGHT: return HLG_LINE_HIGHLIGHT;
        case TOKENIZER_ENABLED_BREAKPOINT: return HLG_ENABLED_BREAKPOINT;
        case TOKENIZER_DISABLED_BREAKPOINT: return HLG_DISABLED_BREAKPOINT;
        case TOKENIZER_SELECTED_LINE_NUMBER: return HLG_SELECTED_LINE_NUMBER;
        case TOKENIZER_ARROW_SEL: return HLG_ARROW_SEL;
        case TOKENIZER_LOGO: return HLG_LOGO;

        case TOKENIZER_NEWLINE: return HLG_LAST;
        default: return HLG_LAST;
    }
}

int highlight_node(const char *filename, struct buffer *buf)
{
    int ret;
    int length = 0;
    int lasttype = -1;
    struct ibuf *ibuf = ibuf_init();
    struct tokenizer *t = tokenizer_init();

    if (tokenizer_set_file(t, filename, buf->language) == -1) {
        if_print_message("%s:%d tokenizer_set_file error", __FILE__, __LINE__);
        return -1;
    }

    while ((ret = tokenizer_get_token(t)) > 0) {
        enum tokenizer_type e = tokenizer_get_packet_type(t);

        /*if_print_message  ( "TOKEN(%d:%s)\n", e, tokenizer_get_printable_enum ( e ) ); */

        if (e == TOKENIZER_NEWLINE) {
            sbpush(buf->tlines, strdup(ibuf_get(ibuf)));

            if (length > buf->max_width)
                buf->max_width = length;

            length = 0;
            lasttype = -1;
            ibuf_clear(ibuf);
        } else {
            const char *tok_data = tokenizer_get_data(t);
            enum hl_group_kind hlg = hlg_from_tokenizer_type(e, tok_data);

            if (hlg == HLG_LAST) {
                logger_write_pos(logger, __FILE__, __LINE__, "Bad hlg_type for '%s', e==%d\n", tok_data, e);
                hlg = HLG_TEXT;
            }

            /* Set the highlight group type */
            add_type(ibuf, &lasttype, hlg);
            /* Add the text and bump our length */
            length += ibuf_add(ibuf, tok_data);
        }
    }

    ibuf_free(ibuf);
    tokenizer_destroy(t);
    return 0;
}

/* --------- */
/* Functions */
/* --------- */

/* highlight_line_segment: Creates a new line that is highlighted.
 * ------------------------
 *
 *  orig:   The line that needs to be highlighted.
 *  start:  The desired starting position of the highlighted portion.
 *          The start index *is* included in the highlighted segment.
 *  end:    The desired ending position of the highlighted portion.
 *          The end index *is not* include in the highlighted segment.
 *
 *  Return Value: Null on error. Or a pointer to a new line that
 *  has highlighting. The new line MUST BE FREED.
 */
/*#define HIGHLIGHT_DEBUG*/
static char *highlight_line_segment(const char *orig, int start, int end)
{
    char *new_line = NULL;
    int length = strlen(orig), j = 0, pos = 0;
    int syn_search_recieved = 0, cur_color = HLG_TEXT;

    /* Cases not possible */
    if (start > end || orig == NULL ||
            start > length || start < 0 || end > length || end < 0)
        return NULL;

    /* The 5 is: color start (2), color end (2) and EOL */
    if ((new_line = (char *) malloc(sizeof (char) * (length + 5))) == NULL)
        return NULL;

#ifdef HIGHLIGHT_DEBUG
    /*
       for ( j = 0; j < strlen(orig); j++ ) {
           char temp[100];
           snprintf(temp, sizeof(temp), "(%d:%c)", orig[j], orig[j]);
           scr_add(gdb_win, temp, 0);
       }
       scr_add(gdb_win, "\r\n", 0);
       scr_refresh(gdb_win, 1, WIN_REFRESH);
     */
#endif

    /* This traverses the input line. It creates a new line with the section
     *     given highlighted.
     * If a highlight symbol is encountered, the end and/or start position 
     *     is incremented because the original match was against only the text,
     *     not against the color embedded text :)
     */
    for (j = 0; j < length; j++) {
        if (orig[j] == HL_CHAR) {
            if (j <= start)
                start += 2;

            if (j <= end)
                end += 2;

            cur_color = orig[j + 1];
        }

        /* Mark when the search is started and when it ends */
        if (j == start) {
            syn_search_recieved = 1;
            new_line[pos++] = HL_CHAR;
            new_line[pos++] = HLG_SEARCH;
        } else if (j == end) {
            syn_search_recieved = 0;
            new_line[pos++] = HL_CHAR;
            new_line[pos++] = cur_color;
        }

        new_line[pos++] = orig[j];

        /* If the search has started, then make all the colors the 
         * highlighted colors  */
        if (syn_search_recieved && orig[j] == HL_CHAR) {
            ++j;
            new_line[pos++] = HLG_SEARCH;
        }
    }

    new_line[pos] = '\0';

#ifdef HIGHLIGHT_DEBUG
    for (j = 0; j < strlen(new_line); j++) {
        char temp[100];

        fprintf(stderr, "(%d:%c)\r\n", new_line[j], new_line[j]);
    }
#endif

    /* Now the line has new information in it.
     * Lets traverse it again and correct the highlighting to be the 
     * reverse.
     */

    return new_line;
}

void hl_wprintw(WINDOW * win, const char *line, int width, int offset, int line_highlight_attr)
{
    int length;                 /* Length of the line passed in */
    enum hl_group_kind color;   /* Color used to print current char */
    int i;                      /* Loops through the line char by char */
    int j;                      /* General iterator */
    int p;                      /* Count of chars printed to screen */
    int pad;                    /* Used to pad partial tabs */
    int attr;                   /* A temp variable used for attributes */
    int highlight_tabstop = cgdbrc_get_int(CGDBRC_TABSTOP);

    /* Jump ahead to the character at offset (process color commands too) */
    length = strlen(line);
    color = HLG_TEXT;

    for (i = 0, j = 0; i < length && j < offset; i++) {
        if (line[i] == HL_CHAR && i + 1 < length) {
            /* Even though we're not printing anything in this loop,
             * the color attribute needs to be maintained for when we
             * start printing in the loop below.  This way the text
             * printed will be done in the correct color. */
            color = (enum hl_group_kind) line[++i];
        } else if (line[i] == '\t') {
            /* Tab character, expand to size set by user */
            j += highlight_tabstop - (j % highlight_tabstop);
        } else {
            /* Normal character, just increment counter by one */
            j++;
        }
    }
    pad = j - offset;

    /* Pad tab spaces if offset is less than the size of a tab */
    for (j = 0, p = 0; j < pad && p < width; j++, p++)
        wprintw(win, " ");

    /* Set the color - if we were given line_highlight, use that, otherwise load */
    if (line_highlight_attr)
        attr = line_highlight_attr;
    else
        hl_groups_get_attr(hl_groups_instance, color, &attr);

    wattron(win, attr);

    /* Print string 1 char at a time */
    for (; i < length && p < width; i++) {
        if (line[i] == HL_CHAR) {
            if (++i < length) {
                /* If we're highlight entire line, skip the line attributes */
                if (!line_highlight_attr) {
                    wattroff(win, attr);
                    color = (enum hl_group_kind) line[i];
                    hl_groups_get_attr(hl_groups_instance, color, &attr);
                    wattron(win, attr);
                }
            }
        } else {
            switch (line[i]) {
                case '\t':
                    do {
                        wprintw(win, " ");
                        p++;
                    } while ((p + offset) % highlight_tabstop > 0 && p < width);
                    break;
                default:
                    wprintw(win, "%c", line[i]);
                    p++;
            }
        }
    }

    /* If we're highlighting line, draw spaces with attributes set */
    if (!line_highlight_attr)
        wattroff(win, attr);

    for (; p < width; p++)
        wprintw(win, " ");

    /* Turn off attributes if we didn't earlier */
    if (line_highlight_attr)
        wattroff(win, attr);
}

int hl_regex(const char *regex, const char **hl_lines, const char **tlines,
        const int length, char **cur_line, int *sel_line,
        int *sel_rline, int *sel_col_rbeg, int *sel_col_rend,
        int opt, int direction, int icase)
{
    regex_t t;                  /* Regular expression */
    regmatch_t pmatch[1];       /* Indexes of matches */
    int i = 0, result = 0;
    char *local_cur_line;
    int success = 0;
    int offset = 0;
    int config_wrapscan = cgdbrc_get_int(CGDBRC_WRAPSCAN);

    if (tlines == NULL || tlines[0] == NULL ||
            cur_line == NULL || sel_line == NULL ||
            sel_rline == NULL || sel_col_rbeg == NULL || sel_col_rend == NULL)
        return -1;

    /* Clear last highlighted line */
    if (*cur_line != NULL) {
        free(*cur_line);
        *cur_line = NULL;
    }

    /* If regex is empty, set current line to original line */
    if (regex == NULL || *regex == '\0') {
        *sel_line = *sel_rline;
        return -2;
    }

    /* Compile the regular expression */
    if (regcomp(&t, regex, REG_EXTENDED & (icase) ? REG_ICASE : 0) != 0) {
        regfree(&t);
        return -3;
    }

    /* Forward search */
    if (direction) {
        int start = *sel_rline;
        int end = length;

        offset = *sel_col_rend;
        while (!success) {
            for (i = start; i < end; i++) {
                int local_cur_line_length;

                local_cur_line = (char *) tlines[i];
                local_cur_line_length = strlen(local_cur_line);

                /* Add the position of the current line's last match */
                if (i == *sel_rline) {
                    if (offset >= local_cur_line_length)
                        continue;
                    local_cur_line += offset;
                }

                /* Found a match */
                if ((result = regexec(&t, local_cur_line, 1, pmatch, 0)) == 0) {
                    success = 1;
                    break;
                }
            }

            if (success || start == 0 || !config_wrapscan) {
                break;
            } else {
                end = start;
                start = 0;
            }
        }

    } else {                    /* Reverse search */
        int j, pos;
        int start = *sel_rline;
        int end = 0;

        offset = *sel_col_rbeg;

        /* Try each line */
        while (!success) {
            for (i = start; i >= end; i--) {
                local_cur_line = (char *) tlines[i];
                pos = strlen(local_cur_line) - 1;
                if (pos < 0)
                    continue;

                if (i == *sel_rline)
                    pos = offset - 1;

                /* Try each line, char by char starting from the end */
                for (j = pos; j >= 0; j--) {
                    if ((result = regexec(&t, local_cur_line + j, 1, pmatch,
                                            0)) == 0) {
                        if (i == *sel_rline && pmatch[0].rm_so > pos - j)
                            continue;
                        /* Found a match */
                        success = 1;
                        offset = j;
                        break;
                    }
                }

                if (success)
                    break;
            }

            if (success || start == length - 1 || !config_wrapscan) {
                break;
            } else {
                end = start;
                start = length - 1;
            }
        }

    }

    if (success) {
        /* The offset is 0 if the line was not on the original line */
        if (direction && *sel_rline != i)
            offset = 0;

        /* If final match ( user hit enter ) make position perminant */
        if (opt == 2) {
            *sel_col_rbeg = pmatch[0].rm_so + offset;
            *sel_col_rend = pmatch[0].rm_eo + offset;
            *sel_rline = i;
        }

        /* Keep the new line as the selected line */
        *sel_line = i;

        /* If the match is not permanent then give cur_line highlighting */
        if (opt != 2 && pmatch[0].rm_so != -1 && pmatch[0].rm_eo != -1)
            *cur_line =
                    highlight_line_segment(hl_lines[i],
                    pmatch[0].rm_so + offset, pmatch[0].rm_eo + offset);
    } else {
        /* On failure, the current line goes to the original line */
        *sel_line = *sel_rline;
    }

    regfree(&t);

    return success;
}
