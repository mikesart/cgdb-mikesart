/* sources.c:
 * ----------
 * 
 * Source file management routines for the GUI.  Provides the ability to
 * add files to the list, load files, and display within a curses window.
 * Files are buffered in memory when they are displayed, and held in
 * memory for the duration of execution.  If memory consumption becomes a
 * problem, this can be optimized to unload files which have not been
 * displayed recently, or only load portions of large files at once. (May
 * affect syntax highlighting.)
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* System Includes */
#if HAVE_STDIO_H
#include <stdio.h>
#endif /* HAVE_STDIO_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if HAVE_CTYPE_H
#include <ctype.h>
#endif

#if HAVE_LIMITS_H
#include <limits.h>             /* CHAR_MAX */
#endif /* HAVE_LIMITS_H */

/* Local Includes */
#include "cgdb.h"
#include "highlight.h"
#include "sources.h"
#include "logo.h"
#include "sys_util.h"
#include "fs_util.h"
#include "cgdbrc.h"
#include "highlight_groups.h"

int sources_syntax_on = 1;

// This speeds up loading sqlite.c from 2:48 down to ~2 seconds.
// sqlite3 is 6,596,401 bytes, 188,185 lines.

/* --------------- */
/* Local Functions */
/* --------------- */

/* get_node:  Returns a pointer to the node that matches the given path.
 * ---------
 
 *   path:  Full path to source file
 *
 * Return Value:  Pointer to the matching node, or NULL if not found.
 */
static struct list_node *get_node(struct sviewer *sview, const char *path)
{
    struct list_node *cur;

    for (cur = sview->list_head; cur != NULL; cur = cur->next) {
        if (cur->path && (strcmp(path, cur->path) == 0))
            return cur;
    }

    return NULL;
}

/**
 * Get's the timestamp of a particular file.
 *
 * \param path
 * The path to the file to get the timestamp of
 *
 * \param timestamp
 * The timestamp of the file, or 0 on error.
 * 
 * \return
 * 0 on success, -1 on error.
 */
static int get_timestamp(const char *path, time_t * timestamp)
{
    struct stat s;
    int val;

    if (!path)
        return -1;

    if (!timestamp)
        return -1;

    *timestamp = 0;

    val = stat(path, &s);

    if (val)                    /* Error on timestamp */
        return -1;

    *timestamp = s.st_mtime;

    return 0;
}

static void init_file_buffer(struct buffer *buf)
{
    buf->tlines = NULL;
    buf->cur_line = NULL;
    buf->max_width = 0;
    buf->file_data = NULL;
    buf->language = TOKENIZER_LANGUAGE_UNKNOWN;
}

static void release_file_buffer(struct buffer *buf)
{
    if (buf) {
        /* Free entire file buffer if we have that */
        if (buf->file_data) {
            free(buf->file_data);
            buf->file_data = NULL;
        }
        else {
            /* Otherwise free individual file lines */
            int i;
            int count = sbcount(buf->tlines);

            for (i = 0; i < count; ++i) {
                free(buf->tlines[i]);
                buf->tlines[i] = NULL;
            }
        }

        sbfree(buf->tlines);
        buf->tlines = NULL;

        free(buf->cur_line);
        buf->cur_line = NULL;

        buf->max_width = 0;
        buf->language = TOKENIZER_LANGUAGE_UNKNOWN;
    }
}

/** 
 * Remove's the memory related to a file.
 *
 * \param node
 * The node who's file buffer data needs to be freed.
 *
 * \return
 * 0 on success, or -1 on error.
 */
static int release_file_memory(struct list_node *node)
{
    if (!node)
        return -1;

    /* Release file buffers */
    release_file_buffer(&node->color_buf);
    release_file_buffer(&node->orig_buf);
    node->buf = NULL;

    return 0;
}

/**
 * Get file size from file pointer.
 *
 * \param file
 * file pointer
 *
 * \return
 * file size on success, or -1 on error.
 */
static long get_file_size(FILE *file)
{
    if (fseek(file, 0, SEEK_END) != -1) {
        long size;

        size = ftell(file);
        fseek(file, 0, SEEK_SET);

        return size;
    }

    return -1;
}

/**
 * Load file and fill tlines line pointers.
 *
 * \param buf
 * struct buffer pointer
 *
 * \param filename
 * name of file to load
 *
 * \return
 * 0 on sucess, 1 on error
 */
static int load_file_buf(struct buffer *buf, const char *filename)
{
    FILE *file;
    long file_size;

    if (!(file = fopen(filename, "r")))
        return 1;

    file_size = get_file_size(file);
    if ( file_size > 0 ) {
        size_t bytes_read;

        buf->file_data = (char *)cgdb_malloc(file_size + 2);
        bytes_read = fread(buf->file_data, 1, file_size, file);
        if ( bytes_read != file_size ) {
            return 1;
        } else {
            char *line_start = buf->file_data;
            char *line_feed = strchr( line_start, '\n' );

            line_start[file_size] = 0;

            while ( line_feed ) {
                size_t line_len;
                char *line_end = line_feed;

                /* Trim trailing cr-lfs */
                while ( line_end >= line_start && ( *line_end == '\n' || *line_end == '\r' ) )
                    *line_end-- = 0;

                /* Update max length string found */
                line_len = line_end - line_start;
                if (line_len > buf->max_width )
                    buf->max_width = line_len;

                /* Add this line to tlines array */
                sbpush( buf->tlines, line_start );

                line_start = line_feed + 1;
                line_feed = strchr( line_start, '\n' );
            }

            if ( *line_start )
                sbpush( buf->tlines, line_start );
        }

        /* Add two nil bytes in case we use lexer string scanner. */
        buf->file_data[file_size] = 0;
        buf->file_data[file_size + 1] = 0;
    }

    fclose(file);
    return 0;
}

/* load_file:  Loads the file in the list_node into its memory buffer.
 * ----------
 *
 *   node:  The list node to work on
 *
 * Return Value:  Zero on success, non-zero on error.
 */
static int load_file(struct list_node *node)
{
    /* Stat the file to get the timestamp */
    if (get_timestamp(node->path, &(node->last_modification)) == -1)
        return 2;

    node->language = tokenizer_get_default_file_type(strrchr(node->path, '.'));

    /* Add the highlighted lines */
    return source_highlight(node);
}

static int get_line_leading_ws_count(const char *otext, int length, int tabstop)
{
    int i;
    int column_offset = 0;      /* Text to skip due to arrow */

    for (i = 0; i < length - 1; i++) {
        int offset;

        /* Skip highlight chars (HL_CHAR / CHAR_MAX) */
        if (otext[i] == CHAR_MAX) {
            i++;
            continue;
        }

        /* Bail if we hit a non whitespace character */
        if (!isspace(otext[i]))
            break;

        /* Add one for space or number of spaces to next tabstop */
        offset = (otext[i] == '\t') ? (tabstop - (column_offset % tabstop)) : 1;
        column_offset += offset;
    }

    return column_offset;
}

/* draw_current_line:  Draws the currently executing source line on the screen
 * ------------------  including the user-selected marker (arrow, highlight,
 *                     etc) indicating this is the executing line.
 *
 *   sview:  The current source viewer
 *   line:   The line number
 *   lwidth: The width of the line number, used to limit printing to the width
 *           of the screen.  Kinda ugly.
 */
static void draw_current_line(struct sviewer *sview, const char *cur_line,
                              int line, int lwidth, int arrow_attr)
{
    int height = 0;             /* Height of curses window */
    int width = 0;              /* Width of curses window */
    int i;                      /* Iterators */
    const char *otext = NULL;   /* The current line (unhighlighted) */
    unsigned int length = 0;    /* Length of the line */
    int column_offset = 0;      /* Text to skip due to arrow */
    int line_highlight_attr = 0;
    int highlight_tabstop = cgdbrc_get_int(CGDBRC_TABSTOP);
    enum ArrowStyle config_arrowstyle = cgdbrc_get_arrowstyle(CGDBRC_ARROWSTYLE);

    /* Initialize height and width */
    getmaxyx(sview->win, height, width);

    otext = sview->cur->buf->tlines[line];
    length = strlen(otext);

    /* Draw the appropriate arrow, if applicable */
    switch (config_arrowstyle) {
        case ARROWSTYLE_SHORT:
            wattron(sview->win, arrow_attr);
            waddch(sview->win, ACS_LTEE);
            waddch(sview->win, '>');
            wattroff(sview->win, arrow_attr);
            break;

        case ARROWSTYLE_LONG:
            wattron(sview->win, arrow_attr);
            waddch(sview->win, ACS_LTEE);

            column_offset = get_line_leading_ws_count(otext, length, highlight_tabstop);
            column_offset -= (sview->cur->sel_col + 1);
            if (column_offset < 0)
                column_offset = 0;

            /* Now actually draw the arrow */
            for (i = 0; i < column_offset; i++)
                waddch(sview->win, ACS_HLINE);

            waddch(sview->win, '>');
            wattroff(sview->win, arrow_attr);
            break;

        case ARROWSTYLE_HIGHLIGHT:
            /* Draw the entire line with the LINE_HIGHLIGHT background attribute */
            waddch(sview->win, VERT_LINE);
            waddch(sview->win, ' ');

            /* Set line_highlight_attr which we'll pass to hl_wprintw */
            hl_groups_get_attr(hl_groups_instance, HLG_LINE_HIGHLIGHT, &line_highlight_attr);
    }

    /* Finally, print the source line */
    hl_wprintw(sview->win, cur_line, width - lwidth - 2,
            sview->cur->sel_col + column_offset, line_highlight_attr);
}

/* --------- */
/* Functions */
/* --------- */

/* Descriptive comments found in header file: sources.h */

int source_highlight(struct list_node *node)
{
    int do_color = sources_syntax_on &&
                   (node->language != TOKENIZER_LANGUAGE_UNKNOWN) &&
                   has_colors();

    node->buf = NULL;

    /* If we're doing color and we haven't already loaded this file
     * with this language, then load and highlight it.
     */
    if (do_color && (node->color_buf.language != node->language))
    {
        /* Release previously loaded data */
        release_file_buffer(&node->color_buf);

        node->color_buf.language = node->language;
        if ( highlight_node(node->path, &node->color_buf) )
        {
            /* Error - free this and try loading no color buffer */
            release_file_buffer( &node->color_buf );
            do_color = 0;
        }
    }

    if (!do_color && !sbcount(node->orig_buf.tlines))
        load_file_buf(&node->orig_buf, node->path);

    /* If we're doing color, use color_buf, otherwise original buf */
    node->buf = do_color ? &node->color_buf : &node->orig_buf;

    /* Allocate the breakpoints array */
    if ( !node->lflags ) {
        int count = sbcount(node->buf->tlines);
        sbsetcount( node->lflags, count );

        memset(node->lflags, 0, sbcount(node->lflags));
    }

    if (node->buf && node->buf->tlines)
        return 0;

    return -1;
}


struct sviewer *source_new(int pos_r, int pos_c, int height, int width)
{
    struct sviewer *rv;

    /* Allocate a new structure */
    rv = (struct sviewer *)cgdb_malloc(sizeof (struct sviewer));

    /* Initialize the structure */
    rv->win = newwin(height, width, pos_r, pos_c);
    rv->cur = NULL;
    rv->list_head = NULL;

    /* Initialize global marks */
    memset(rv->global_marks, 0, sizeof(rv->global_marks));
    rv->jump_back_mark.node = NULL;
    rv->jump_back_mark.line = -1;

    return rv;
}

int source_add(struct sviewer *sview, const char *path)
{
    struct list_node *new_node;

    new_node = (struct list_node *)cgdb_malloc(sizeof (struct list_node));
    new_node->path = strdup(path);

    init_file_buffer(&new_node->orig_buf);
    init_file_buffer(&new_node->color_buf);

    new_node->buf = NULL;
    new_node->lflags = NULL;
    new_node->sel_line = 0;
    new_node->sel_col = 0;
    new_node->sel_col_rbeg = 0;
    new_node->sel_col_rend = 0;
    new_node->sel_rline = 0;
    new_node->exe_line = -1;
    new_node->last_modification = 0;    /* No timestamp yet */

    /* Initialize all local marks to -1 */
    memset(new_node->local_marks, 0xff, sizeof(new_node->local_marks));

    if (sview->list_head == NULL) {
        /* List is empty, this is the first node */
        new_node->next = NULL;
        sview->list_head = new_node;
    } else {
        /* Insert at the front of the list (easy) */
        new_node->next = sview->list_head;
        sview->list_head = new_node;
    }

    return 0;
}

int source_del(struct sviewer *sview, const char *path)
{
    int i;
    struct list_node *cur;
    struct list_node *prev = NULL;

    /* Find the target node */
    for (cur = sview->list_head; cur != NULL; cur = cur->next) {
        if (strcmp(path, cur->path) == 0)
            break;
        prev = cur;
    }

    if (cur == NULL)
        return 1;               /* Node not found */

    /* Release file buffers */
    release_file_buffer(&cur->orig_buf);
    release_file_buffer(&cur->color_buf);
    cur->buf = NULL;

    /* Release file name */
    free(cur->path);
    cur->path = NULL;

    sbfree(cur->lflags);
    cur->lflags = NULL;

    /* Remove link from list */
    if (cur == sview->list_head)
        sview->list_head = sview->list_head->next;
    else
        prev->next = cur->next;

    /* Free the node */
    free(cur);

    /* Free any global marks pointing to this bugger */
    for (i = 0; i < sizeof(sview->global_marks) / sizeof(sview->global_marks[0]); i++) {
        if (sview->global_marks[i].node == cur)
            sview->global_marks[i].node = NULL;
    }

    return 0;
}

int source_length(struct sviewer *sview, const char *path)
{
    struct list_node *cur = get_node(sview, path);

    if (!cur) {
        /* Load the file if it's not already */
        if (!cur->buf && load_file(cur))
            return -1;
    }

    return sbcount(cur->buf->tlines);
}

char *source_current_file(struct sviewer *sview, char *path)
{
    if (sview == NULL || sview->cur == NULL)
        return NULL;

    strcpy(path, sview->cur->path);
    return path;
}

int source_get_mark_char(struct sviewer *sview, int line)
{
    if ((line < 0) || (line >= sbcount(sview->cur->lflags)))
        return -1;

    if (sview->cur->lflags[line].has_mark) {
        int i;

        for (i = 0; i < MARK_COUNT; i++) {
            if (sview->global_marks[i].line == line)
                return 'A' + i;
        }

        for (i = 0; i < MARK_COUNT; i++) {
            if (sview->cur->local_marks[i] == line)
                return 'a' + i;
        }
    }

    return -1;
}

int source_set_mark(struct sviewer *sview, int key)
{
    int ret = 0;
    int old_line;
    int sel_line = sview->cur->sel_line;

    if (key >= 'a' && key <= 'z') {
        /* Local buffer mark */
        old_line = sview->cur->local_marks[key - 'a'];
        sview->cur->local_marks[key - 'a'] = sel_line;
        ret = 1;
    } else if (key >= 'A' && key <= 'Z') {
        /* Global buffer mark */
        old_line = sview->global_marks[key - 'A'].line;
        sview->global_marks[key - 'A'].line = sel_line;
        sview->global_marks[key - 'A'].node = sview->cur;
        ret = 1;
    }

    if (ret) {
        /* Just added a mark to the selected line, flag it */
        sview->cur->lflags[sel_line].has_mark = 1;

        /* Check if the old line still has a mark */
        if (source_get_mark_char(sview, old_line) < 0)
            sview->cur->lflags[old_line].has_mark = 0;
    }

    return ret;
}

int source_goto_mark(struct sviewer *sview, int key)
{
    int line;
    struct list_node *node = NULL;

    if (key >= 'a' && key <= 'z') {
        /* Local buffer mark */
        line = sview->cur->local_marks[key - 'a'];
	node = (line >= 0) ? sview->cur : NULL;
    } else if (key >= 'A' && key <= 'Z') {
        /* Global buffer mark */
        line = sview->global_marks[key - 'A'].line;
        node = sview->global_marks[key - 'A'].node;
    } else if (key == '\'' ) {
        /* Jump back to where we jumped from */
        line = sview->jump_back_mark.line;
        node = sview->jump_back_mark.node;
    } else if (key == '.') {
        /* Jump to currently executing line if it's set */
        line = sview->cur->exe_line;
        node = (line >= 0) ? sview->cur : NULL;
    }

    if (node) {
        sview->jump_back_mark.line = sview->cur->sel_line;
        sview->jump_back_mark.node = sview->cur;

        sview->cur = node;
        source_set_sel_line(sview, line + 1);
        return 1;
    }

    return 0;
}

int source_display(struct sviewer *sview, int focus, enum win_refresh dorefresh)
{
    char fmt[5];
    int width, height;
    int lwidth;
    int line;
    int i;
    int count;
    int focus_attr = focus ? A_BOLD : 0;
    int arrow_selected_line;
    int sellineno_attr;
    int enabled_bp_attr, disabled_bp_attr;
    int arrow_sel_attr, arrow_attr;

    /* Check that a file is loaded */
    if (sview->cur == NULL || sview->cur->buf->tlines == NULL) {
        logo_display(sview->win);

        if (dorefresh == WIN_REFRESH)
            wrefresh(sview->win);
        else
            wnoutrefresh(sview->win);
        return 0;
    }

    hl_groups_get_attr(hl_groups_instance, HLG_SELECTED_LINE_NUMBER, &sellineno_attr);
    hl_groups_get_attr(hl_groups_instance, HLG_ENABLED_BREAKPOINT, &enabled_bp_attr);
    hl_groups_get_attr(hl_groups_instance, HLG_DISABLED_BREAKPOINT, &disabled_bp_attr);
    hl_groups_get_attr(hl_groups_instance, HLG_ARROW, &arrow_attr);
    hl_groups_get_attr(hl_groups_instance, HLG_ARROW_SEL, &arrow_sel_attr);

    /* Make sure cursor is visible */
    curs_set(!!focus);

    /* Initialize variables */
    getmaxyx(sview->win, height, width);

    /* Set starting line number (center source file if it's small enough) */
    count = sbcount(sview->cur->buf->tlines);
    if (count < height)
        line = (count - height) / 2;
    else {
        line = sview->cur->sel_line - height / 2;
        if (line > count - height)
            line = count - height;
        else if (line < 0)
            line = 0;
    }

    /* Print 'height' lines of the file, starting at 'line' */
    lwidth = log10_uint(count) + 1;
    snprintf(fmt, sizeof(fmt), "%%%dd", lwidth);

    arrow_selected_line = focus && cgdbrc_get_int(CGDBRC_ARROWSELECTEDLINE);

    for (i = 0; i < height; i++, line++) {
        int is_sel_line;
        int is_exe_line;
        const char *cur_line;

        wmove(sview->win, i, 0);

        if (!has_colors()) {
            wprintw(sview->win, "%s\n", sview->cur->buf->tlines[line]);
            continue;
        }

        /* Outside of file, just draw the vertical line */
        if (line < 0 || line >= count) {
            int j;

            for (j = 1; j < lwidth; j++)
                waddch(sview->win, ' ');
            waddch(sview->win, '~');

            wattron(sview->win, focus_attr);
            waddch(sview->win, VERT_LINE);
            wattroff(sview->win, focus_attr);

            wclrtoeol(sview->win);
            continue;
        }

        /* Is this the current selected line? */
        is_sel_line = (sview->cur->sel_line == line);
        /* Is this the current executing line */
        is_exe_line = (sview->cur->exe_line == line);
        /* Get line we should print */
        cur_line = (is_sel_line && sview->cur->buf->cur_line) ?
                       sview->cur->buf->cur_line : sview->cur->buf->tlines[line];

        /* Mark the current line with an arrow or the selected line if in focus and arrowalllines is on */
        if ( is_exe_line || (arrow_selected_line && is_sel_line) ) {
            int attr;

            if (sview->cur->lflags[line].breakpt == 0)
                attr = (is_sel_line && !is_exe_line) ? arrow_sel_attr : arrow_attr;
            else
                attr = (sview->cur->lflags[line].breakpt == 1) ? enabled_bp_attr : disabled_bp_attr;

            /* Print line number */
            wattron(sview->win, attr);
            wprintw(sview->win, fmt, line + 1);
            wattroff(sview->win, attr);

            draw_current_line(sview, cur_line, line, lwidth, attr);

            /* Look for breakpoints */
        } else if (sview->cur->lflags[line].breakpt) {
            int attr = (sview->cur->lflags[line].breakpt == 1) ? enabled_bp_attr : disabled_bp_attr;

            /* Print line number */
            wattron(sview->win, attr);
            wprintw(sview->win, fmt, line + 1);
            wattroff(sview->win, attr);

            /* Vertical bar */
            wattron(sview->win, focus_attr);
            waddch(sview->win, VERT_LINE);
            wattroff(sview->win, focus_attr);
            waddch(sview->win, ' ');

            hl_wprintw(sview->win, cur_line, width - lwidth - 2, sview->cur->sel_col, 0);
        }
        /* Ordinary lines */
        else {
            int lineno_attr = (focus && is_sel_line) ? sellineno_attr : 0;

            /* Print line number */
            wattron(sview->win, lineno_attr);
            wprintw(sview->win, fmt, line + 1);
            wattroff(sview->win, lineno_attr);

            /* Vertical bar */
            wattron(sview->win, focus_attr);
            waddch(sview->win, VERT_LINE);
            wattroff(sview->win, focus_attr);
            waddch(sview->win, ' ');

            /* No special line information */
            hl_wprintw(sview->win, cur_line, width - lwidth - 2, sview->cur->sel_col, 0);
        }

        if (cgdbrc_get_int(CGDBRC_SHOWMARKS)) {
            /* Show marks if option is set */
            int mark_char = source_get_mark_char(sview, line);

            if (mark_char > 0) {
                wmove(sview->win, i, lwidth);

                wattron(sview->win, arrow_attr);
                waddch(sview->win, mark_char);
                wattroff(sview->win, arrow_attr);
            }
        }
    }

    wmove(sview->win, height - (line - sview->cur->sel_line), lwidth + 2);

    if (dorefresh == WIN_REFRESH)
        wrefresh(sview->win);
    else
        wnoutrefresh(sview->win);
    return 0;
}

void source_move(struct sviewer *sview,
        int pos_r, int pos_c, int height, int width)
{
    delwin(sview->win);
    sview->win = newwin(height, width, pos_r, pos_c);
    werase(sview->win);
}

static int clamp_line(struct sviewer *sview, int line)
{
    if (line < 0)
        line = 0;
    if (line >= sbcount(sview->cur->buf->tlines))
        line = sbcount(sview->cur->buf->tlines) - 1;

    return line;
}

void source_vscroll(struct sviewer *sview, int offset)
{
    if (sview->cur) {
        sview->cur->sel_line = clamp_line(sview, sview->cur->sel_line + offset);
        sview->cur->sel_rline = sview->cur->sel_line;
    }
}

void source_hscroll(struct sviewer *sview, int offset)
{
    int lwidth;
    int max_width;
    int width, height;

    if (sview->cur) {
        getmaxyx(sview->win, height, width);

        lwidth = log10_uint(sbcount(sview->cur->buf->tlines)) + 1;
        max_width = sview->cur->buf->max_width - width + lwidth + 6;

        sview->cur->sel_col += offset;
        if (sview->cur->sel_col > max_width)
            sview->cur->sel_col = max_width;
        if (sview->cur->sel_col < 0)
            sview->cur->sel_col = 0;
    }
}

void source_set_sel_line(struct sviewer *sview, int line)
{
    if (sview->cur) {
        /* Set line (note correction for 0-based line counting) */
        sview->cur->sel_line = clamp_line(sview, line - 1);
        sview->cur->sel_rline = sview->cur->sel_line;
    }
}

int source_set_exec_line(struct sviewer *sview, const char *path, int sel_line, int exe_line)
{
    if (path && !fs_verify_file_exists(path))
        return 5;

    /* Locate node, if path has changed */
    if (path != NULL && !(sview->cur = get_node(sview, path))) {
        /* Not found -- attempt to add */
        if (source_add(sview, path))
            return 1;
        else if (!(sview->cur = get_node(sview, path)))
            return 2;
    } else if (path == NULL && sview->cur == NULL)
        return 3;

    /* Buffer the file if it's not already */
    if (!sview->cur->buf && load_file(sview->cur))
        return 4;

    /* Update line, if set */
    if (sel_line--) {
        sview->cur->sel_line = clamp_line(sview, sel_line);

        /* Set executing line if passed a valid value */
        if (exe_line > 0)
            sview->cur->exe_line = clamp_line(sview, exe_line - 1);
    }

    return 0;
}

void source_free(struct sviewer *sview)
{
    /* Free all file buffers */
    while (sview->list_head != NULL)
        source_del(sview, sview->list_head->path);

    delwin(sview->win);
    sview->win = NULL;

    free(sview);
}

void source_search_regex_init(struct sviewer *sview)
{
    if (sview == NULL || sview->cur == NULL)
        return;

    /* Start from beginning of line if not at same line */
    if (sview->cur->sel_rline != sview->cur->sel_line) {
        sview->cur->sel_col_rend = 0;
        sview->cur->sel_col_rbeg = 0;
    }

    /* Start searching at the beginning of the selected line */
    sview->cur->sel_rline = sview->cur->sel_line;
}

int source_search_regex(struct sviewer *sview,
        const char *regex, int opt, int direction, int icase)
{
    if (sview == NULL || sview->cur == NULL ||
        regex == NULL || strlen(regex) == 0) {

        if (sview && sview->cur) {
            free(sview->cur->buf->cur_line);
            sview->cur->buf->cur_line = NULL;
        }
        return -1;
    }

    if (!sbcount(sview->cur->orig_buf.tlines))
        load_file_buf(&sview->cur->orig_buf, sview->cur->path);

    return hl_regex(regex,
            (const char **) sview->cur->buf->tlines,
            (const char **) sview->cur->orig_buf.tlines,
            sbcount(sview->cur->buf->tlines),
            &sview->cur->buf->cur_line, &sview->cur->sel_line,
            &sview->cur->sel_rline, &sview->cur->sel_col_rbeg,
            &sview->cur->sel_col_rend, opt, direction, icase);
}

void source_disable_break(struct sviewer *sview, const char *path, int line)
{
    struct list_node *node;

    node = get_node(sview, path);
    if (!node)
        return;

    if (!node->buf && load_file(node))
        return;

    if (line > 0 && line <= sbcount(node->lflags))
        node->lflags[line - 1].breakpt = 2;
}

void source_enable_break(struct sviewer *sview, const char *path, int line)
{
    struct list_node *node;

    node = get_node(sview, path);
    if (!node)
        return;

    if (!node->buf && load_file(node))
        return;

    if (line > 0 && line < sbcount(node->lflags)) {
        node->lflags[line - 1].breakpt = 1;
    }
}

void source_clear_breaks(struct sviewer *sview)
{
    struct list_node *node;

    for (node = sview->list_head; node != NULL; node = node->next)
    {
        int i;
        for (i = 0; i < sbcount(node->lflags); i++)
            node->lflags[i].breakpt = 0;
    }
}

int source_reload(struct sviewer *sview, const char *path, int force)
{
    time_t timestamp;
    struct list_node *cur;
    struct list_node *prev = NULL;
    int auto_source_reload = cgdbrc_get_int(CGDBRC_AUTOSOURCERELOAD);

    if (!path)
        return -1;

    if (get_timestamp(path, &timestamp) == -1)
        return -1;

    /* Find the target node */
    for (cur = sview->list_head; cur != NULL; cur = cur->next) {
        if (strcmp(path, cur->path) == 0)
            break;
        prev = cur;
    }

    if (cur == NULL)
        return 1;               /* Node not found */

    if ((auto_source_reload || force) && cur->last_modification < timestamp) {

        if (release_file_memory(sview->cur) == -1)
            return -1;

        if (load_file(cur))
            return -1;
    }

    return 0;
}
