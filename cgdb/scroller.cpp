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
#include "cgdb.h"
#include "cgdbrc.h"
#include "highlight_groups.h"
#include "scroller.h"
#include "sys_util.h"

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

    for (i = 0; i < slen; i++) {
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
static char *parse(struct scroller *scr, struct scroller_line_attr **attrs,
    const char *orig, const char *buf, int buflen)
{
    // Read in tabstop settings, but don't change them on the fly as we'd have to
    //  store each previous line and recalculate every one of them.
    static const int tab_size = cgdbrc_get_int(CGDBRC_TABSTOP);
    int tabcount = count(buf, buflen, '\t');
    int orig_len = strlen(orig);
    int length = MAX(orig_len, scr->current.pos) + buflen + (tab_size - 1) * tabcount;
    char *rv = (char *) cgdb_calloc(length + 1, 1);
    int i, j;

    /* Copy over original buffer */
    strcpy(rv, orig);

    i = scr->current.pos;

    /* Expand special characters */
    for (j = 0; j < buflen; j++) {
        int attr;

        /* Handle ansi escape characters */
        if (buf[j] == '\033') {
            int ansi_count = hl_ansi_get_color_attrs(buf + j, &attr);
            if (ansi_count) {
                struct scroller_line_attr line_attr;
                j += ansi_count - 1;

                line_attr.col = i;
                line_attr.attr = attr;
                sbpush(*attrs, line_attr);
                continue;
            }
        }

        switch (buf[j]) {
                /* Backspace/Delete -> Erase last character */
            case 8:
            case 127:
                if (i > 0)
                    i--;
                break;
                /* Tab -> Translating to spaces */
            case '\t':
                do {
                    rv[i++] = ' ';
                } while (i % tab_size != 0);
                break;
                /* Carriage return -> Move back to the beginning of the line */
            case '\r':
                i = 0;
                if (buf[j + 1] != '\n') {
                    sbfree(*attrs);
                    *attrs = NULL;
                }
                break;
                /* Default case -> Only keep printable characters */
            default:
                if (!iscntrl((int) buf[j])) {
                    rv[i] = buf[j];
                    i++;
                }
                break;
        }
    }

    scr->current.pos = i;

    /* Remove trailing space from the line if we don't have color */
    if (*attrs) {
        j = strlen(rv);
    } else {
        for (j = strlen(rv) - 1; j > i && isspace(rv[j]); j--)
            ;
    }

    /* Only realloc if it's going to save us more than ~128 bytes */
    return (length - j > 128) ? (char *)cgdb_realloc(rv, j + 2) : rv;
}

static void scroller_set_last_tty_attr(struct scroller *scr)
{
    int index = sbcount(scr->lines) - 1;
    struct scroller_line *sl = index >= 0 ? &scr->lines[index] : 0;

    /* If this is a tty line and we've got color attributes */
    if (sl && sl->tty && sbcount(sl->attrs)) {
        /* Grab last attribute */
        int attr = sl->attrs[sbcount(sl->attrs) - 1].attr;

        /* Store last attribute for following tty lines */
        scr->last_tty_attr = attr ? attr : -1;
    }
}

static void scroller_addline(struct scroller *scr, char *line,
    struct scroller_line_attr *attrs, int tty)
{
    struct scroller_line sl;

    /* Add attribute from last tty line to start of this one */
    if (tty && (scr->last_tty_attr != -1)) {
        /* If there isn't already a color attribute for the first column */
        if (!attrs || (attrs[0].col != 0)) {
            int count = sbcount(attrs);

            /* Bump the count up and scoot the attributes over one */
            sbsetcount(attrs, count + 1);
            memmove(attrs+1, attrs, count * sizeof(struct scroller_line_attr));

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

struct scroller *scr_new(int pos_r, int pos_c, int height, int width)
{
    struct scroller *rv;

    if ((rv = (struct scroller *)cgdb_malloc(sizeof (struct scroller))) == NULL)
        return NULL;

    rv->current.r = 0;
    rv->current.c = 0;
    rv->current.pos = 0;
    rv->in_scroll_mode = 0;
    rv->clear_row = -1;
    rv->last_tty_line = NULL;
    rv->last_tty_attr = -1;
    rv->win = newwin(height, width, pos_r, pos_c);

    /* Start with a single (blank) line */
    rv->lines = NULL;
    scroller_addline(rv, strdup(""), NULL, 0);
    return rv;
}

void scr_free(struct scroller *scr)
{
    int i;

    /* Release the buffer */
    for (i = 0; i < sbcount(scr->lines); i++) {
        free(scr->lines[i].line);
        sbfree(scr->lines[i].attrs);
    }
    sbfree(scr->lines);
    scr->lines = NULL;

    free(scr->last_tty_line);
    scr->last_tty_line = NULL;

    delwin(scr->win);
    scr->win = NULL;

    /* Release the scroller object */
    free(scr);
}

void scr_up(struct scroller *scr, int nlines)
{
    int height, width;
    int length;
    int i;

    scr->in_scroll_mode = 1;

    /* Sanity check */
    getmaxyx(scr->win, height, width);
    if (scr->current.c > 0) {
        if (scr->current.c % width != 0)
            scr->current.c = (scr->current.c / width) * width;
    }

    for (i = 0; i < nlines; i++) {
        /* If current column is positive, drop it by 'width' */
        if (scr->current.c > 0)
            scr->current.c -= width;

        /* Else, decrease the current row number, and set column accordingly */
        else {
            if (scr->current.r > 0) {
                scr->current.r--;
                if ((length = strlen(scr->lines[scr->current.r].line)) > width)
                    scr->current.c = ((length - 1) / width) * width;
            } else {
                /* At top */
                break;
            }
        }
    }
}

void scr_down(struct scroller *scr, int nlines)
{
    int height, width;
    int length;
    int i;

    int at_bottom = (scr->current.r == (sbcount(scr->lines) - 1));
    if (at_bottom)
        scr->in_scroll_mode = 0;

    /* Sanity check */
    getmaxyx(scr->win, height, width);
    if (scr->current.c > 0) {
        if (scr->current.c % width != 0)
            scr->current.c = (scr->current.c / width) * width;
    }

    for (i = 0; i < nlines; i++) {
        /* If the current line wraps to the next, then advance column number */
        length = strlen(scr->lines[scr->current.r].line);
        if (scr->current.c < length - width)
            scr->current.c += width;

        /* Otherwise, advance row number, and set column number to 0. */
        else {
            if (scr->current.r < sbcount(scr->lines) - 1) {
                scr->current.r++;
                scr->current.c = 0;
            } else {
                /* At bottom */
                at_bottom = 1;
                break;
            }
        }
    }
}

void scr_home(struct scroller *scr)
{
    scr->current.r = 0;
    scr->current.c = 0;

    scr->in_scroll_mode = 1;
}

void scr_end(struct scroller *scr)
{
    int height, width;

    getmaxyx(scr->win, height, width);

    scr->current.r = sbcount(scr->lines) - 1;
    scr->current.c = (strlen(scr->lines[scr->current.r].line) / width) * width;

    scr->in_scroll_mode = 0;
}

static void scr_add_buf(struct scroller *scr, const char *buf, int tty)
{
    char *x;
    int distance;

    /* Find next newline in the string */
    x = strchr((char *)buf, '\n');
    distance = x ? x - buf : strlen(buf);

    /* Append to the last line in the buffer */
    if (distance > 0) {
        int is_crlf = (distance == 1) && (buf[0] == '\r');
        if (!is_crlf) {
            int index = sbcount(scr->lines) - 1;
            char *orig = scr->lines[index].line;

            if ((scr->last_tty_attr != -1) && (tty != scr->lines[index].tty)) {
                struct scroller_line_attr attr;

                attr.col = strlen(orig);
                if (tty) {
                    attr.attr = scr->last_tty_attr;
                    scr->last_tty_attr = -1;
                }
                else {
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
    while (x != NULL) {
        char *line;
        struct scroller_line_attr *attrs = NULL;

        buf = x + 1;
        x = strchr((char *)buf, '\n');
        distance = x ? x - buf : strlen(buf);

        /* tty input with no lf. */
        if (!x && tty && distance && (distance < 4096)) {
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

    scr_end(scr);
}

void scr_add(struct scroller *scr, const char *buf, int tty)
{
    char *tempbuf = NULL;

    if (scr->last_tty_line) {
        if (!tty) {
            /* New line coming in isn't tty so spew out last tty line and carry on */
            scr_add_buf(scr, scr->last_tty_line, 1);
            free(scr->last_tty_line);
        }
        else {
            /* Combine this tty line with previous tty line */
            tempbuf = (char *)cgdb_realloc(scr->last_tty_line, strlen(scr->last_tty_line) + strlen(buf) + 1);
            strcat(tempbuf, buf);
            buf = tempbuf;
        }

        scr->last_tty_line = NULL;
    }

    scr_add_buf(scr, buf, tty);

    scr_end(scr);

    free(tempbuf);
}

void scr_move(struct scroller *scr, int pos_r, int pos_c, int height, int width)
{
    delwin(scr->win);
    scr->win = newwin(height, width, pos_r, pos_c);
    wclear(scr->win);
}

static void scr_printline(WINDOW *win, int x, int y, int col,
    struct scroller_line *sline, int width)
{
    int attr = 0;
    int line_len = sline->line_len;
    const char *line = sline->line;
    int count = MIN(line_len - col, width);

    wmove(win, y, x);

    if (sline->attrs) {
        int i;

        for (i = 0; i < sbcount(sline->attrs); i++) {
            if (sline->attrs[i].col <= col) {
                attr = sline->attrs[i].attr;
            }
            else if (sline->attrs[i].col < col + count) {
                int len = sline->attrs[i].col -col;

                wattron(win, attr);
                wprintw(win, "%.*s", len, line + col);
                wattroff(win, attr);

                col += len;
                count -= len;
                width -= len;

                attr = sline->attrs[i].attr;
            } else {
                wattron(win, attr);
                wprintw(win, "%.*s", count, line + col);
                wattroff(win, attr);

                width -= count;
                count = 0;
            }
        }
    }

    if (count) {
        wattron(win, attr);
        wprintw(win, "%.*s", count, line + col);
        wattroff(win, attr);

        width -= count;
    }

    if (width)
        wclrtoeol(win);
}

void scr_refresh(struct scroller *scr, int focus)
{
    int length;                 /* Length of current line */
    int nlines;                 /* Number of lines written so far */
    int r;                      /* Current row in scroller */
    int c;                      /* Current column in row */
    int width, height;          /* Width and height of window */
    int highlight_attr;

    /* Steal line highlight attribute for our scroll mode status */
    hl_groups_get_attr(hl_groups_instance, HLG_LINE_HIGHLIGHT, &highlight_attr);

    /* Sanity check */
    getmaxyx(scr->win, height, width);

    if (scr->current.c > 0) {
        if (scr->current.c % width != 0)
            scr->current.c = (scr->current.c / width) * width;
    }
    r = scr->current.r;
    c = scr->current.c;

    /* Start drawing at the bottom of the viewable space, and work our way up */
    for (nlines = 1; nlines <= height; nlines++) {

        if ((r <= scr->clear_row) && !scr->in_scroll_mode)
            r = -1;

        /* Print the current line [segment] */
        if (r >= 0) {
            struct scroller_line *sline = &scr->lines[r];

            scr_printline(scr->win, 0, height - nlines, c, sline, width);

            /* Update our position */
            if (c >= width)
                c -= width;
            else {
                r--;
                if (r >= 0) {
                    length = scr->lines[r].line_len;
                    if (length > width)
                        c = ((length - 1) / width) * width;
                }
            }
        } else {
            wmove(scr->win, height - nlines, 0);
            wclrtoeol(scr->win);
        }

        /* If we're in scroll mode and this is the top line, spew status on right */
        if (scr->in_scroll_mode && (nlines == height)) {
            char status[ 64 ];
            size_t status_len;

            snprintf(status, sizeof(status), "[%d/%d]", scr->current.r + 1, sbcount(scr->lines));

            status_len = strlen(status);
            if ( status_len < width ) {
                wattron(scr->win, highlight_attr);
                mvwprintw(scr->win, height - nlines, width - status_len, "%s", status);
                wattroff(scr->win, highlight_attr);
            }
        }
    }

    length = scr->lines[scr->current.r].line_len - scr->current.c;
    if (focus && scr->current.r == sbcount(scr->lines) - 1 && length <= width) {
        /* We're on the last line, draw the cursor */
        curs_set(1);
        wmove(scr->win, height - 1, scr->current.pos % width);
    } else {
        /* Hide the cursor */
        curs_set(0);
    }

    wrefresh(scr->win);
}
