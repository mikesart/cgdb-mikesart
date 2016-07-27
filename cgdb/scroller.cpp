/* scroller.c:
 * -----------
 *
 * A scrolling buffer utility.  Able to add and subtract to the buffer.
 * All routines that would require a screen update will automatically refresh
 * the scroller.
 */

/* Local Includes */
#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* System Includes */
#if HAVE_CTYPE_H
#include <ctype.h>
#endif

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

/* Local Includes */
#include "sys_util.h"
#include "sys_win.h"
#include "cgdb.h"
#include "cgdbrc.h"
#include "highlight_groups.h"
#include "scroller.h"
#include "highlight.h"

/* --------------- */
/* Local Functions */
/* --------------- */

/* count: Count the occurrences of a character c in a string s.
 * ------
 *
 *   s:  String to search
 *   c:  Character to search for
 *
 * Return Value:  Number of occurrences of c in s.
 */
static int count(const char *s, int slen, char c)
{
    int i;
    int rv = 0;

    for (i = 0; i < slen; i++)
    {
        rv += (s[i] == c);
    }

    return rv;
}

/* parse: Translates special characters in a string.  (i.e. backspace, tab...)
 * ------
 *
 *   buf:  The string to parse
 *
 * Return Value:  A newly allocated copy of buf, with modifications made.
 */
static char *parse(struct scroller *scr, struct hl_line_attr **attrs,
    const char *orig, const char *buf, int buflen)
{
    // Read in tabstop settings, but don't change them on the fly as we'd have to
    //  store each previous line and recalculate every one of them.
    static const int tab_size = cgdbrc_get_int(CGDBRC_TABSTOP);
    int tabcount = count(buf, buflen, '\t');
    int orig_len = strlen(orig);
    int length = MAX(orig_len, scr->current.pos) + buflen + (tab_size - 1) * tabcount;
    char *rv = (char *)cgdb_calloc(length + 1, 1);
    int i, j;

    /* Copy over original buffer */
    strcpy(rv, orig);

    i = scr->current.pos;

    /* Expand special characters */
    for (j = 0; j < buflen; j++)
    {
        int attr;

        /* Handle ansi escape characters */
        if (buf[j] == '\033')
        {
            int ansi_count = hl_ansi_get_color_attrs(hl_groups_instance, buf + j, &attr, 0);
            if (ansi_count)
            {
                struct hl_line_attr line_attr;
                j += ansi_count - 1;

                line_attr.col = i;
                line_attr.attr = attr;
                sbpush(*attrs, line_attr);
                continue;
            }
        }

        switch (buf[j])
        {
        /* Backspace/Delete -> Erase last character */
        case 8:
        case 127:
            if (i > 0)
                i--;
            break;
        /* Tab -> Translating to spaces */
        case '\t':
            do
            {
                rv[i++] = ' ';
            } while (i % tab_size != 0);
            break;
        /* Carriage return -> Move back to the beginning of the line */
        case '\r':
            i = 0;
            if (buf[j + 1] != '\n')
            {
                sbfree(*attrs);
                *attrs = NULL;
            }
            break;
        /* Default case -> Only keep printable characters */
        default:
            if (!iscntrl((int)buf[j]))
            {
                rv[i] = buf[j];
                i++;
            }
            break;
        }
    }

    scr->current.pos = i;

    if (*attrs)
    {
        j = strlen(rv);
    }
    else
    {
        /* Remove trailing space from the line if we don't have color */
        for (j = strlen(rv) - 1; j > i && isspace(rv[j]); j--)
            ;
        rv[j + 1] = 0;
    }

    /* Only realloc if it's going to save us more than ~128 bytes */
    return (length - j > 128) ? (char *)cgdb_realloc(rv, j + 2) : rv;
}

static void scroller_set_last_tty_attr(struct scroller *scr)
{
    int index = sbcount(scr->lines) - 1;
    struct scroller_line *sl = index >= 0 ? &scr->lines[index] : 0;

    /* If this is a tty line and we've got color attributes */
    if (sl && sl->tty && sbcount(sl->attrs))
    {
        /* Grab last attribute */
        int attr = sl->attrs[sbcount(sl->attrs) - 1].attr;

        /* Store last attribute for following tty lines */
        scr->last_tty_attr = attr ? attr : -1;
    }
}

static void scroller_addline(struct scroller *scr, char *line,
    struct hl_line_attr *attrs, int tty)
{
    struct scroller_line sl;

    /* Add attribute from last tty line to start of this one */
    if (tty && (scr->last_tty_attr != -1))
    {
        /* If there isn't already a color attribute for the first column */
        if (!attrs || (attrs[0].col != 0))
        {
            int count = sbcount(attrs);

            /* Bump the count up and scoot the attributes over one */
            sbsetcount(attrs, count + 1);
            memmove(attrs + 1, attrs, count * sizeof(struct hl_line_attr));

            attrs[0].col = 0;
            attrs[0].attr = scr->last_tty_attr;
        }

        scr->last_tty_attr = -1;
    }

    sl.line = line;
    sl.line_len = strlen(line);
    sl.tty = tty;
    sl.attrs = attrs;
    sbpush(scr->lines, sl);

    scroller_set_last_tty_attr(scr);
}

/* ----------------- */
/* Exposed Functions */
/* ----------------- */

/* See scroller.h for function descriptions. */

struct scroller *scr_new()
{
    struct scroller *rv;

    rv = (struct scroller *)cgdb_malloc(sizeof(struct scroller));

    rv->current.r = 0;
    rv->current.c = 0;
    rv->current.pos = 0;
    rv->in_scroll_mode = 0;
    rv->clear_row = -1;
    rv->last_tty_line = NULL;
    rv->last_tty_attr = -1;
    rv->width = 0;

    rv->in_search_mode = 0;
    rv->hlregex = NULL;
    rv->regex_is_searching = 0;
    rv->search_r = 0;

    /* Start with a single (blank) line */
    rv->lines = NULL;
    scroller_addline(rv, strdup(""), NULL, 0);

    rv->jump_back_mark.r = -1;
    rv->jump_back_mark.c = -1;
    memset(rv->local_marks, 0xff, sizeof(rv->local_marks));
    memset(rv->global_marks, 0xff, sizeof(rv->global_marks));

    return rv;
}

void scr_free(struct scroller *scr)
{
    int i;

    /* Release the buffer */
    for (i = 0; i < sbcount(scr->lines); i++)
    {
        free(scr->lines[i].line);
        sbfree(scr->lines[i].attrs);
    }
    sbfree(scr->lines);
    scr->lines = NULL;

    hl_regex_free(&scr->hlregex);
    scr->hlregex = NULL;
    scr->regex_is_searching = 0;

    free(scr->last_tty_line);
    scr->last_tty_line = NULL;

    /* Release the scroller object */
    free(scr);
}

static int get_last_col(struct scroller *scr, int row)
{
    return (MAX(scr->lines[row].line_len - 1, 0) / scr->width) * scr->width;
}

static void scr_scroll_lines(struct scroller *scr, int *r, int *c, int nlines)
{
    int i;
    int row = *r;
    int col = (*c / scr->width) * scr->width;
    int amt = (nlines < 0) ? -scr->width : scr->width;

    if (nlines < 0)
        nlines = -nlines;

    for (i = 0; i < nlines; i++)
    {
        col += amt;

        if (col < 0)
        {
            if (row <= 0)
                break;

            row--;
            col = get_last_col(scr, row);
        }
        else if (col >= scr->lines[row].line_len)
        {
            if (row >= sbcount(scr->lines) - 1)
                break;

            row++;
            col = 0;
        }

        *r = row;
        *c = col;
    }
}

void scr_up(struct scroller *scr, int nlines)
{
    scr->in_scroll_mode = 1;

    scr_scroll_lines(scr, &scr->current.r, &scr->current.c, -nlines);
}

void scr_down(struct scroller *scr, int nlines)
{
    int at_bottom = (scr->current.r == (sbcount(scr->lines) - 1));

    if (at_bottom)
        scr->in_scroll_mode = 0;

    scr_scroll_lines(scr, &scr->current.r, &scr->current.c, nlines);
}

void scr_home(struct scroller *scr)
{
    scr->current.r = 0;
    scr->current.c = 0;

    scr->in_scroll_mode = 1;
}

void scr_end(struct scroller *scr)
{
    scr->current.r = sbcount(scr->lines) - 1;
    scr->current.c = get_last_col(scr, scr->current.r);
}

static void scr_add_buf(struct scroller *scr, const char *buf, int tty)
{
    char *x;
    int distance;

    /* Find next newline in the string */
    x = strchr((char *)buf, '\n');
    distance = x ? x - buf : strlen(buf);

    /* Append to the last line in the buffer */
    if (distance > 0)
    {
        int is_crlf = (distance == 1) && (buf[0] == '\r');
        if (!is_crlf)
        {
            int index = sbcount(scr->lines) - 1;
            char *orig = scr->lines[index].line;
            int orig_len = scr->lines[index].line_len;

            if ((scr->last_tty_attr != -1) && (tty != scr->lines[index].tty))
            {
                struct hl_line_attr attr;

                attr.col = orig_len;
                if (tty)
                {
                    attr.attr = scr->last_tty_attr;
                    scr->last_tty_attr = -1;
                }
                else
                {
                    /* Add that color attribute in */
                    attr.attr = 0;
                }

                sbpush(scr->lines[index].attrs, attr);
            }

            scr->lines[index].tty = tty;
            scr->lines[index].line = parse(scr, &scr->lines[index].attrs, orig, buf, distance);
            scr->lines[index].line_len = strlen(scr->lines[index].line);

            scroller_set_last_tty_attr(scr);

            free(orig);
        }
    }

    /* Create additional lines if buf contains newlines */
    while (x != NULL)
    {
        char *line;
        struct hl_line_attr *attrs = NULL;

        buf = x + 1;
        x = strchr((char *)buf, '\n');
        distance = x ? x - buf : strlen(buf);

        /* tty input with no lf. */
        if (!x && tty && distance && (distance < 4096))
        {
            /* Store away and parse when the rest of the line shows up */
            scr->last_tty_line = strdup(buf);
            /* Add line since we did have a lf */
            scr->current.pos = 0;
            scroller_addline(scr, strdup(""), NULL, tty);
            break;
        }

        /* Expand the buffer */
        scr->current.pos = 0;

        /* Add the new line */
        line = parse(scr, &attrs, "", buf, distance);
        scroller_addline(scr, line, attrs, tty);
    }

    /* Move to end of buffer and exit scroll mode */
    scr_end(scr);
    scr->in_scroll_mode = 0;
}

void scr_add(struct scroller *scr, const char *buf, int tty)
{
    char *tempbuf = NULL;

    if (scr->last_tty_line)
    {
        if (!tty)
        {
            /* New line coming in isn't tty so spew out last tty line and carry on */
            scr_add_buf(scr, scr->last_tty_line, 1);
            free(scr->last_tty_line);
        }
        else
        {
            /* Combine this tty line with previous tty line */
            tempbuf = (char *)cgdb_realloc(scr->last_tty_line,
                strlen(scr->last_tty_line) + strlen(buf) + 1);
            strcat(tempbuf, buf);
            buf = tempbuf;
        }

        scr->last_tty_line = NULL;
    }

    scr_add_buf(scr, buf, tty);

    /* Move to end of buffer and exit scroll mode */
    scr_end(scr);
    scr->in_scroll_mode = 0;

    free(tempbuf);
}

void scr_search_regex_init(struct scroller *scr)
{
    scr->in_search_mode = 1;

    /* Start searching at the beginning of the selected line */
    scr->search_r = scr->current.r;
}

static int wrap_line(struct scroller *scr, int line)
{
    int count = sbcount(scr->lines);

    if (line < 0)
        line = count - 1;
    else if (line >= count)
        line = 0;

    return line;
}

int scr_search_regex(struct scroller *scr, const char *regex, int opt,
    int direction, int icase)
{
    /* If we've got a regex, store the opt value:
     *   1: searching
     *   2: done searching
     */
    scr->regex_is_searching = (regex && regex[0]) ? opt : 0;

    if (scr->regex_is_searching)
    {
        int line;
        int line_end;
        int line_inc = direction ? +1 : -1;
        int line_start = scr->search_r;

        line = wrap_line(scr, line_start + line_inc);

        if (cgdbrc_get_int(CGDBRC_WRAPSCAN))
            line_end = line_start;
        else
            line_end = direction ? sbcount(scr->lines) : -1;

        for (;;)
        {
            int ret;
            int start, end;
            char *line_str = scr->lines[line].line;

            ret = hl_regex_search(&scr->hlregex, line_str, regex, icase, &start, &end);
            if (ret > 0)
            {
                /* Got a match */
                scr->current.r = line;
                scr->current.c = get_last_col(scr, line);

                /* Finalized match - move to this location */
                if (opt == 2)
                    scr->search_r = line;
                return 1;
            }

            line = wrap_line(scr, line + line_inc);
            if (line == line_end)
                break;
        }
    }

    /* Nothing found - go back to original line */
    scr->current.r = scr->search_r;
    scr->current.c = get_last_col(scr, scr->search_r);
    return 0;
}

int scr_set_mark(struct scroller *scr, int key)
{
    if (key >= 'a' && key <= 'z')
    {
        /* Local buffer mark */
        scr->local_marks[key - 'a'].r = scr->current.r;
        scr->local_marks[key - 'a'].c = scr->current.c;
        return 1;
    }
    else if (key >= 'A' && key <= 'Z')
    {
        /* Global buffer mark */
        scr->global_marks[key - 'A'].r = scr->current.r;
        scr->global_marks[key - 'A'].c = scr->current.c;
        return 1;
    }

    return 0;
}

int scr_goto_mark(struct scroller *scr, int key)
{
    scroller_mark mark_temp;
    scroller_mark *mark = NULL;

    if (key >= 'a' && key <= 'z')
    {
        /* Local buffer mark */
        mark = &scr->local_marks[key - 'a'];
    }
    else if (key >= 'A' && key <= 'Z')
    {
        /* Global buffer mark */
        mark = &scr->global_marks[key - 'A'];
    }
    else if (key == '\'')
    {
        /* Jump back to where we last jumped from */
        mark_temp = scr->jump_back_mark;
        mark = &mark_temp;
    }
    else if (key == '.')
    {
        /* Jump to last line */
        mark_temp.r = sbcount(scr->lines) - 1;
        mark_temp.c = get_last_col(scr, scr->current.r);
        mark = &mark_temp;
    }

    if (mark && (mark->r >= 0))
    {
        scr->jump_back_mark.r = scr->current.r;
        scr->jump_back_mark.c = scr->current.c;

        scr->current.r = mark->r;
        scr->current.c = mark->c;
        return 1;
    }

    return 0;
}

void scr_refresh(struct scroller *scr, SWINDOW *win, int focus, enum win_refresh dorefresh)
{
    int length;        /* Length of current line */
    int nlines;        /* Number of lines written so far */
    int row;           /* Current row in scroller */
    int col;           /* Current column in row */
    int width, height; /* Width and height of window */
    int highlight_attr;

    /* Steal line highlight attribute for our scroll mode status */
    hl_groups_get_attr(hl_groups_instance, HLG_LINE_HIGHLIGHT, &highlight_attr);

    /* Sanity check */
    height = swin_getmaxy(win);
    width = swin_getmaxx(win);

    scr->current.c = (scr->current.c / width) * width;

    row = scr->current.r;
    col = scr->current.c;

    /* Start drawing at the bottom of the viewable space, and work our way up */
    for (nlines = 1; nlines <= height; nlines++)
    {
        if ((row <= scr->clear_row) && !scr->in_scroll_mode)
            row = -1;

        /* Print the current line [segment] */
        if (row >= 0)
        {
            struct scroller_line *sline = &scr->lines[row];

            hl_printline(win, sline->line, sline->line_len, sline->attrs,
                0, height - nlines, col, width);

            /* If we're searching right now or we finished search and have focus... */
            if ((scr->regex_is_searching == 1) || (scr->regex_is_searching == 2 && focus))
            {
                struct hl_line_attr *attrs;

                attrs = hl_regex_highlight(&scr->hlregex, sline->line);

                if (sbcount(attrs))
                {
                    hl_printline_highlight(win, sline->line, sline->line_len,
                        attrs, 0, height - nlines, col, width);
                    sbfree(attrs);
                }
            }

            /* Update our position */
            if (col >= width)
            {
                col -= width;
            }
            else
            {
                row--;
                if (row >= 0)
                {
                    length = scr->lines[row].line_len;
                    if (length > width)
                        col = ((length - 1) / width) * width;
                }
            }
        }
        else
        {
            swin_wmove(win, height - nlines, 0);
            swin_wclrtoeol(win);
        }

        /* If we're in scroll mode and this is the top line, spew status on right */
        if (scr->in_scroll_mode && (nlines == height))
        {
            char status[64];
            size_t status_len;

            snprintf(status, sizeof(status), "[%d/%d]", scr->current.r + 1, sbcount(scr->lines));

            status_len = strlen(status);
            if (status_len < width)
            {
                swin_wattron(win, highlight_attr);
                swin_mvwprintw(win, height - nlines, width - status_len, "%s", status);
                swin_wattroff(win, highlight_attr);
            }
        }
    }

    length = scr->lines[scr->current.r].line_len - scr->current.c;
    if (focus && scr->current.r == sbcount(scr->lines) - 1 && length <= width)
    {
        /* We're on the last line, draw the cursor */
        swin_curs_set(1);
        swin_wmove(win, height - 1, scr->current.pos % width);
    }
    else
    {
        /* Hide the cursor */
        swin_curs_set(0);
    }

    if (dorefresh == WIN_REFRESH)
        swin_wrefresh(win);
    else
        swin_wnoutrefresh(win);
}
