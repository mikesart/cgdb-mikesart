/* scroller.h:
 * -----------
 *
 * A scrolling buffer utility.  Able to add and subtract to the buffer.
 * All routines that would require a screen update will automatically redraw
 * the scroller.  There is no "draw" function.
 */

#ifndef _SCROLLER_H_
#define _SCROLLER_H_

/* Count of marks */
#define MARK_COUNT 26

/* --------------- */
/* Data Structures */
/* --------------- */

struct scroller_line
{
    char *line;
    int line_len;
    int tty;
    struct hl_line_attr *attrs;
};

struct scroller_mark
{
    int r;
    int c;
};

struct scroller
{
    struct scroller_line *lines;

    char *last_tty_line; /* Partial tty line - without \n */
    int last_tty_attr;   /* ansi attribute we got for last tty line */
    int in_scroll_mode;  /* Currently in scroll mode? */
    int clear_row;       /* Row where clear (ctrl+L) was hit */
    struct
    {
        int r;   /* Current line (row) number */
        int c;   /* Current column number */
        int pos; /* Cursor position in last line */
    } current;

    int width;   /* Current window width */

    int in_search_mode;
    struct hl_regex_info *hlregex;
    int regex_is_searching;
    int search_r;

    scroller_mark local_marks[MARK_COUNT]; /* Global A-Z marks */
    scroller_mark global_marks[MARK_COUNT]; /* Global A-Z marks */
    scroller_mark jump_back_mark;           /* Location where last jump occurred from */
};

/* --------- */
/* Functions */
/* --------- */

/* scr_new: Creates and initializes a new scroller
 * --------
 *
 * Return Value: A pointer to a new scroller, or NULL on error.
 */
struct scroller *scr_new();

/* scr_free: Releases the memory allocated by a scroller
 * ---------
 *
 *   scr:  Pointer to the scroller object
 */
void scr_free(struct scroller *scr);

/* scr_up: Move up a number of lines
 * -------
 *
 *   scr:    Pointer to the scroller object
 *   nlines: Number of lines to scroll back; will not scroll past beginning
 */
void scr_up(struct scroller *scr, int nlines);

/* scr_down: Move down a number of lines
 * ---------
 *
 *   scr:    Pointer to the scroller object
 *   nlines: Number of lines to scroll down; will not scroll past end
 */
void scr_down(struct scroller *scr, int nlines);

/* scr_home: Jump to the top line of the buffer
 * ---------
 *
 *   scr:  Pointer to the scroller object
 */
void scr_home(struct scroller *scr);

/* scr_end: Jump to the bottom line of the buffer
 * --------
 *
 *   scr:  Pointer to the scroller object
 */
void scr_end(struct scroller *scr);

/* scr_add:  Append a string to the buffer.
 * --------
 *
 *   scr:  Pointer to the scroller object
 *   buf:  Buffer to append -- \b characters will be treated as backspace!
 */
void scr_add(struct scroller *scr, const char *buf, int tty);

/* scr_refresh: Refreshes the scroller on the screen, in case the caller
 * ------------ damages the screen area where the scroller is written (or,
 *              perhaps the terminal size has changed, and you wish to redraw).
 *
 *   scr:    Pointer to the scroller object
 *   focus:  If the window has focus
 */
void scr_refresh(struct scroller *scr, SWINDOW *win, int focus, enum win_refresh dorefresh);

void scr_search_regex_init(struct scroller *scr);
int scr_search_regex(struct scroller *scr, const char *regex, int opt,
    int direction, int icase);

int scr_set_mark(struct scroller *scr, int key);
int scr_goto_mark(struct scroller *scr, int key);

#endif
