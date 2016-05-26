#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* System Includes */
#if HAVE_CTYPE_H
#include <ctype.h>
#endif

#include "highlight_groups.h"
#include "command_lexer.h"
#include "sys_util.h"
#include "logger.h"

#if HAVE_CURSES_H
#include <curses.h>
#elif HAVE_NCURSES_CURSES_H
#include <ncurses/curses.h>
#endif /* HAVE_CURSES_H */

#include <strings.h>
#include <stdlib.h>

/* internal {{{*/

#define UNSPECIFIED_COLOR (-2)

/** This represents all the data for a particular highlighting group. */
struct hl_group_info {
  /** The kind of group */
    enum hl_group_kind kind;
  /** The attributes for a terminal, if it has no color support. */
    int mono_attrs;
  /** The attributes for a terminal, if it has color support. */
    int color_attrs;
  /** The id ncurses uses to represent a color-pair */
    int color_pair;
};

/** The main context used to represent all of the highlighting groups. */
struct hl_groups {
  /** If 0 then the terminal doesn't support colors, otherwise it does. */
    int in_color;
  /** If 1 then the terminal supports ansi colors (8 colors, 64 color pairs). */
    int ansi_color;
  /** This is the data for each highlighting group. */
    struct hl_group_info groups[HLG_LAST];
};

static struct hl_group_info *lookup_group_info_by_key(struct hl_groups *groups,
        enum hl_group_kind kind)
{
    if (groups) {
        int i;
        for (i = 0; i < HLG_LAST; ++i) {
            struct hl_group_info *lgroups = &groups->groups[i];
            if (kind == lgroups->kind)
                return lgroups;
        }
    }

    return NULL;
}

/** The global instance, this is used externally */
hl_groups_ptr hl_groups_instance = NULL;

/** 
 * This describes all the attributes that a highlighting group can represent.
 * It is used to represent the default value for a group.
 */
struct default_hl_group_info {
  /** The kind of group */
    enum hl_group_kind kind;
  /** Same as description above. */
    int mono_attrs;
  /** Same as description above. */
    int color_attrs;
  /** The foreground number, representing the color. */
    int fore_color;
  /** The background number, representing the color. */
    int back_color;
};

/** 
 * The default colors and attributes when use_default_colors is not available.
 * This means that -1 can not be used to represent the default color 
 * background. Currently, in this case, black is used as the color. This 
 * could eventually be configurable.
 */
static const struct default_hl_group_info default_groups_for_curses[] = {
    {HLG_KEYWORD, A_BOLD, A_BOLD, COLOR_BLUE, COLOR_BLACK},
    {HLG_TYPE, A_BOLD, A_BOLD, COLOR_GREEN, COLOR_BLACK},
    {HLG_LITERAL, A_BOLD, A_BOLD, COLOR_RED, COLOR_BLACK},
    {HLG_COMMENT, A_NORMAL, A_NORMAL, COLOR_YELLOW, COLOR_BLACK},
    {HLG_DIRECTIVE, A_BOLD, A_BOLD, COLOR_CYAN, COLOR_BLACK},
    {HLG_TEXT, A_NORMAL, A_NORMAL, COLOR_WHITE, COLOR_BLACK},
    {HLG_SEARCH, A_NORMAL, A_NORMAL, COLOR_BLACK, COLOR_WHITE},
    {HLG_STATUS_BAR, A_NORMAL, A_NORMAL, COLOR_BLACK, COLOR_WHITE},
    {HLG_ARROW, A_BOLD, A_BOLD, COLOR_GREEN, COLOR_BLACK},
    {HLG_LINE_HIGHLIGHT, A_BOLD, A_BOLD, COLOR_BLACK, COLOR_GREEN},
    {HLG_ENABLED_BREAKPOINT, A_BOLD, A_BOLD, COLOR_RED, COLOR_BLACK},
    {HLG_DISABLED_BREAKPOINT, A_BOLD, A_BOLD, COLOR_YELLOW, COLOR_BLACK},
    {HLG_SELECTED_LINE_NUMBER, A_BOLD, A_BOLD, COLOR_WHITE, COLOR_BLACK},
    {HLG_ARROW_SEL, A_BOLD, A_BOLD, COLOR_WHITE, COLOR_BLACK},
    {HLG_LOGO, A_BOLD, A_BOLD, COLOR_BLUE, COLOR_BLACK},
    {HLG_LAST, A_NORMAL, A_NORMAL, -1, -1}
};

/** 
 * The default colors and attributes for each type of token. 
 * This is used when ncurses is available.
 */
static const struct default_hl_group_info default_groups_for_background_dark[]
        = {
    {HLG_KEYWORD, A_BOLD, A_BOLD, COLOR_BLUE, -1},
    {HLG_TYPE, A_BOLD, A_BOLD, COLOR_GREEN, -1},
    {HLG_LITERAL, A_BOLD, A_BOLD, COLOR_RED, -1},
    {HLG_COMMENT, A_NORMAL, A_NORMAL, COLOR_YELLOW, -1},
    {HLG_DIRECTIVE, A_BOLD, A_BOLD, COLOR_CYAN, -1},
    {HLG_TEXT, A_NORMAL, A_NORMAL, -1, -1},
    {HLG_SEARCH, A_REVERSE, A_REVERSE, -1, -1},
    {HLG_STATUS_BAR, A_REVERSE, A_REVERSE, -1, -1},
    {HLG_ARROW, A_BOLD, A_BOLD, COLOR_GREEN, -1},
    {HLG_LINE_HIGHLIGHT, A_BOLD, A_BOLD, COLOR_BLACK, COLOR_GREEN},
    {HLG_ENABLED_BREAKPOINT, A_BOLD, A_BOLD, COLOR_RED, -1},
    {HLG_DISABLED_BREAKPOINT, A_BOLD, A_BOLD, COLOR_YELLOW, -1},
    {HLG_SELECTED_LINE_NUMBER, A_BOLD, A_BOLD, -1, -1},
    {HLG_ARROW_SEL, A_BOLD, A_BOLD, -1, -1},
    {HLG_LOGO, A_BOLD, A_BOLD, COLOR_BLUE, -1},
    {HLG_LAST, A_NORMAL, A_NORMAL, -1, -1}
};

struct hl_group_name {
  /** The kind of group */
    enum hl_group_kind kind;
  /** The name of the group */
    const char *name;
};

static struct hl_group_name hl_group_names[] = {
    {HLG_KEYWORD, "Statement"},
    {HLG_TYPE, "Type"},
    {HLG_LITERAL, "Constant"},
    {HLG_COMMENT, "Comment"},
    {HLG_DIRECTIVE, "PreProc"},
    {HLG_TEXT, "Normal"},
    {HLG_SEARCH, "IncSearch"},
    {HLG_STATUS_BAR, "StatusLine"},
    {HLG_ARROW, "Arrow"},
    {HLG_LINE_HIGHLIGHT, "LineHighlight"},
    {HLG_ENABLED_BREAKPOINT, "Breakpoint"},
    {HLG_DISABLED_BREAKPOINT, "DisabledBreakpoint"},
    {HLG_SELECTED_LINE_NUMBER, "SelectedLineNr"},
    {HLG_ARROW_SEL, "SelectedLineArrow"},
    {HLG_LOGO, "Logo"},
    {HLG_LAST, NULL}
};

/**
 * Simply lookup the hl_group_kind from the name.
 *
 * \param name
 * The name to look up.
 *
 * \param kind
 * If the name is valid, kind will be returned as the group kind.
 *
 * \return 
 * 0 on success, -1 on error.
 */
static int
get_hl_group_kind_from_name(const char *name, enum hl_group_kind *kind)
{
    int i;

    if (!name || !kind)
        return -1;

    for (i = 0; hl_group_names[i].name != NULL; ++i)
        if (strcasecmp(name, hl_group_names[i].name) == 0) {
            *kind = hl_group_names[i].kind;
            return 0;
        }

    return -1;
}

/** This maps a particular attribute to a name. */
struct attr_pair {
  /** The name of the attribute. */
    const char *name;
  /** The attribute value */
    int value;
};

/** The list of terminal attributes that CGDB supports */
static const struct attr_pair attr_names[] = {
    {"bold", A_BOLD},
    {"underline", A_UNDERLINE},
    {"reverse", A_REVERSE},
    {"inverse", A_REVERSE},
    {"standout", A_STANDOUT},
    {"NONE", A_NORMAL},
    {"normal", A_NORMAL},
    {"blink", A_BLINK},
    {"dim", A_DIM},
    {NULL, 0}
};

const struct attr_pair *lookup_attr_pair_by_name(const char *name)
{
    int i;

    for (i = 0; attr_names[i].name != NULL; ++i)
        if (strcasecmp(name, attr_names[i].name) == 0)
            return &attr_names[i];

    return NULL;
}

/** A structure to represent a specific color. */
struct color_info {
    /* The name of the color */
    const char *name;
    /* The number this color represents if the terminal supports 8 colors. */
    int nr8Color;
    /* If the terminal has 8 colors, they could be bold. */
    int nr8ForegroundBold;
    /* HLG group id for this color */
    enum hl_group_kind hlg_type;
};

/** A list of all the default colors and their values */
static const struct color_info hl_color_names[] = {
    {"Black", COLOR_BLACK, 0, HLG_BLACK},
    {"DarkBlue", COLOR_BLUE, 0, HLG_BLUE},
    {"DarkGreen", COLOR_GREEN, 0, HLG_GREEN},
    {"DarkCyan", COLOR_CYAN, 0, HLG_CYAN},
    {"DarkRed", COLOR_RED, 0, HLG_RED},
    {"DarkMagenta", COLOR_MAGENTA, 0, HLG_MAGENTA},
    {"Brown", COLOR_YELLOW, 0, HLG_YELLOW},
    {"DarkYellow", COLOR_YELLOW, 0, HLG_YELLOW},
    {"LightGray", COLOR_WHITE, 0, HLG_WHITE},
    {"LightGrey", COLOR_WHITE, 0, HLG_WHITE},
    {"Gray", COLOR_WHITE, 0, HLG_WHITE},
    {"Grey", COLOR_WHITE, 0, HLG_WHITE},
    // Bold/high-intensity colors
    {"DarkGray", COLOR_BLACK, 1, HLG_BOLD_BLACK},
    {"DarkGrey", COLOR_BLACK, 1, HLG_BOLD_BLACK},
    {"Blue", COLOR_BLUE, 1, HLG_BOLD_BLUE},
    {"LightBlue", COLOR_BLUE, 1, HLG_BOLD_BLUE},
    {"Green", COLOR_GREEN, 1, HLG_BOLD_GREEN},
    {"LightGreen", COLOR_GREEN, 1, HLG_BOLD_GREEN},
    {"Cyan", COLOR_CYAN, 1, HLG_BOLD_CYAN},
    {"LightCyan", COLOR_CYAN, 1, HLG_BOLD_CYAN},
    {"Red", COLOR_RED, 1, HLG_BOLD_RED},
    {"LightRed", COLOR_RED, 1, HLG_BOLD_RED},
    {"Magenta", COLOR_MAGENTA, 1, HLG_BOLD_MAGENTA},
    {"LightMagenta", COLOR_MAGENTA, 1, HLG_BOLD_MAGENTA},
    {"Yellow", COLOR_YELLOW, 1, HLG_BOLD_YELLOW},
    {"LightYellow", COLOR_YELLOW, 1, HLG_BOLD_YELLOW},
    {"White", COLOR_WHITE, 1, HLG_BOLD_WHITE},
    {NULL, 0, 0, HLG_LAST}
};

static const struct color_info *color_spec_for_name(const char *name)
{
    int i;

    for (i = 0; hl_color_names[i].name != NULL; ++i) {
        if (strcasecmp(name, hl_color_names[i].name) == 0)
            return &hl_color_names[i];
    }

    return NULL;
}

enum hl_group_kind hl_get_color_group(hl_groups_ptr hl_group, const char *color)
{
    const struct color_info *color_info = color_spec_for_name(color);

    return color_info ? color_info->hlg_type : HLG_LAST;
}

/* Given an ncurses COLOR_XX background and foreground color, return an ncurses
 *  color pair index for that color.
 */
static int hl_get_color_pair(hl_groups_ptr hl_groups, int bgcolor, int fgcolor)
{
    static int color_pairs_inited = 0;
    static int color_pair_table[9][9];

    /* If ansi colors aren't enabled, return default color pair 0 */
    if (!hl_groups->ansi_color)
        return 0;

    if (!color_pairs_inited) {
        int fg, bg;
        int color_pair = 1;

        /* Initialize 64 [1..8][1..8] color entries */
        for (fg = COLOR_BLACK; fg <= COLOR_WHITE; fg++) {
            for (bg = COLOR_BLACK; bg <= COLOR_WHITE; bg++) {
                init_pair(color_pair, fg, bg);
                color_pair_table[bg + 1][fg + 1] = color_pair++;
            }
        }

        /* init_pair:
         *  The value of the first argument must be between 1 and COLOR_PAIRS-1,
         * except that if default colors are used (see use_default_colors) the
         * upper limit is adjusted to allow for extra pairs which use a default
         * color in foreground and/or background.
         */

        /* Initialize colors with default bg: [0][1..8] */
        for (fg = COLOR_BLACK; fg <= COLOR_WHITE; fg++) {
            init_pair(color_pair, fg, -1);
            color_pair_table[0][fg + 1] = color_pair++;
        }
        /* Initialize colors with default fg: [1..8][0] */
        for (bg = COLOR_BLACK; bg <= COLOR_WHITE; bg++) {
            init_pair(color_pair, -1, bg);
            color_pair_table[bg + 1][0] = color_pair++;
        }

        color_pairs_inited = 1;
    }

    fgcolor = MAX(0, fgcolor + 1);
    bgcolor = MAX(0, bgcolor + 1);
    return color_pair_table[bgcolor][fgcolor];
}

/**
 * Set up a highlighting group to be displayed as the user wishes.
 *
 * \param group
 * The highlighting group
 *
 * \param mono_attrs
 * Attributes avialable in mono mode
 *
 * \param color_attrs
 * Attributes available during color mode
 *
 * \param fore_color
 * The foreground color
 *
 * \param back_color
 * The background color
 *
 * \return
 * 0 on success, -1 on error.
 */
static int
setup_group(hl_groups_ptr hl_groups, enum hl_group_kind group,
        int mono_attrs, int color_attrs, int fore_color, int back_color)
{
  /** Starts creating new colors at 1, and then is incremented each time. */
    static int next_color_pair = 1;
    struct hl_group_info *info;

    info = lookup_group_info_by_key(hl_groups, group);
    if (!info)
        return -1;

    if (mono_attrs != UNSPECIFIED_COLOR)
        info->mono_attrs = mono_attrs;
    if (color_attrs != UNSPECIFIED_COLOR)
        info->color_attrs = color_attrs;

    /* The rest of this function sets up the colors, so we can stop here
     * if color isn't used. */
    if (!hl_groups->in_color)
        return 0;

    /* If no colors are specified, we're done. */
    if (fore_color == UNSPECIFIED_COLOR && back_color == UNSPECIFIED_COLOR)
        return 0;

#ifdef NCURSES_VERSION
    if (hl_groups->ansi_color) {
        /* Ansi mode is enabled so we've got 16 colors and 64 color pairs.
           Set the color_pair index for this bg / fg color combination. */
        info->color_pair = hl_get_color_pair(hl_groups, back_color, fore_color);
        return 0;
    }
#endif

    /* Don't allow -1 to be used in curses mode */
#ifndef NCURSES_VERSION
    if (fore_color < 0 || back_color < 0)
        return 0;
#endif

    /* If either the foreground or background color is unspecified, we
     * need to read the other so we don't clobber it. */
    if (fore_color == UNSPECIFIED_COLOR) {
        short old_fore_color, old_back_color;

        pair_content(info->color_pair, &old_fore_color, &old_back_color);
        fore_color = old_fore_color;
    } else if (back_color == UNSPECIFIED_COLOR) {
        short old_fore_color, old_back_color;

        pair_content(info->color_pair, &old_fore_color, &old_back_color);
        back_color = old_back_color;
    }

    /* If the group is already using the default colors, and we're not changing
     * it, don't consume a color pair. */
    if (fore_color < 0 && back_color < 0 && info->color_pair == 0)
        return 0;

    /* Allocate a new color pair if the group doesn't have one yet. */
    if (info->color_pair == 0) {
        info->color_pair = next_color_pair;
        next_color_pair += 1;
    }

    /* Set up the color pair. */
    if (info->color_pair < COLOR_PAIRS) {
        if (init_pair(info->color_pair, fore_color, back_color) != OK)
            return -1;
    } else
        return -1;

    return 0;
}

/* }}}*/

/* Creating and Destroying a hl_groups context. {{{*/
/*@{*/

hl_groups_ptr hl_groups_initialize(void)
{
    int i;
    hl_groups_ptr hl_groups = (hl_groups_ptr) cgdb_malloc(sizeof (struct hl_groups));

    hl_groups->in_color = 0;
    hl_groups->ansi_color = 0;

    for (i = 0; i < HLG_LAST; ++i) {
        struct hl_group_info *info;

        info = &hl_groups->groups[i];
        info->kind = (enum hl_group_kind) (i + 1);
        info->mono_attrs = 0;
        info->color_attrs = 0;
        info->color_pair = 0;
    }

    return hl_groups;
}

int hl_groups_shutdown(hl_groups_ptr hl_groups)
{
    if (hl_groups) {
        free(hl_groups);
        hl_groups = NULL;
    }

    return 0;
}

/*@}*/
/* }}}*/

/* Functional commands {{{*/

/*@{*/

int hl_groups_setup(hl_groups_ptr hl_groups)
{
    int i;
    const struct default_hl_group_info *ginfo;

    if (!hl_groups)
        return -1;

#ifdef NCURSES_VERSION
    ginfo = default_groups_for_background_dark;
#else
    ginfo = default_groups_for_curses;
#endif

    hl_groups->in_color = has_colors();

    //$ TODO: Add ability to disable ansi_color mode in cgdbrc file
    hl_groups->ansi_color = has_colors() && (COLORS >= 8) && (COLOR_PAIRS >= 64);

    /* Set up the default groups. */
    for (i = 0; ginfo[i].kind != HLG_LAST; ++i) {
        int val;
        const struct default_hl_group_info *spec = &ginfo[i];

        val = setup_group(hl_groups, spec->kind, spec->mono_attrs,
                spec->color_attrs, spec->fore_color, spec->back_color);
        if (val == -1) {
            logger_write_pos(logger, __FILE__, __LINE__, "setup group.");
            return -1;
        }
    }

    return 0;
}

int
hl_groups_get_attr(hl_groups_ptr hl_groups, enum hl_group_kind kind, int *attr)
{
    switch(kind)
    {
    case HLG_BLACK:
    case HLG_RED:
    case HLG_GREEN:
    case HLG_YELLOW:
    case HLG_BLUE:
    case HLG_MAGENTA:
    case HLG_CYAN:
    case HLG_WHITE:
        /* Return ncurses color pair attribute for color */
        *attr = COLOR_PAIR(hl_get_color_pair(hl_groups, -1, kind - HLG_BLACK));
        return 0;
    case HLG_BOLD_BLACK:
    case HLG_BOLD_RED:
    case HLG_BOLD_GREEN:
    case HLG_BOLD_YELLOW:
    case HLG_BOLD_BLUE:
    case HLG_BOLD_MAGENTA:
    case HLG_BOLD_CYAN:
    case HLG_BOLD_WHITE:
        /* Return ncurses color pair and BOLD attribute for color */
        *attr = A_BOLD | COLOR_PAIR(hl_get_color_pair(hl_groups, -1, kind - HLG_BOLD_BLACK));
        return 0;
    default:
        break;
    }

    struct hl_group_info *info = lookup_group_info_by_key(hl_groups, kind);

    if (!info) {
        /* Default to normal, or bold if this was a highlight */
        *attr = (kind == HLG_LINE_HIGHLIGHT) ? A_BOLD : A_NORMAL;
        return -1;
    }

    if (!hl_groups->in_color) {
        *attr = info->mono_attrs;
    } else {
        *attr = info->color_attrs;
        if (info->color_pair)
            *attr |= COLOR_PAIR(info->color_pair);
    }

    return 0;
}

int hl_groups_parse_config(hl_groups_ptr hl_groups)
{
    int token, val;
    const char *name;
    int mono_attrs = UNSPECIFIED_COLOR, color_attrs = UNSPECIFIED_COLOR;
    int fg_color = UNSPECIFIED_COLOR, bg_color = UNSPECIFIED_COLOR;
    int key, attrs, color;
    const struct color_info *color_spec;
    enum hl_group_kind group_kind;
    const struct attr_pair *pair;
    enum {
        TERM,
        CTERM,
        FG,
        BG,
        IGNORE
    };

    if (!hl_groups)
        return 1;

    /* First, get the "group", that is, the group. */
    token = yylex();
    if (token != IDENTIFIER) {
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
        logger_write_pos(logger, __FILE__, __LINE__, "Missing group name.");
#endif
        return 1;
    }

    name = get_token();

    val = get_hl_group_kind_from_name(name, &group_kind);
    if (val == -1) {
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
        logger_write_pos(logger, __FILE__, __LINE__,
                "Bad parameter name (\"%s\").", name);
#endif
        return 1;
    }

    if (group_kind < 0) {
        /* Just ignore groups we don't know about (or "link", which is not
         * a group, but which could appear here too). */
        return 0;
    }

    /* Now parse the settings for the group. */
    /* At the end of each key/value(s) pair, "token" will be set to the *next*
     * token.  That's because we have to deal with comma-separated lists. */
    token = yylex();
    while (1) {
        /* Get the next key. */
        if (token == 0 || token == EOL)
            break;
        if (token != IDENTIFIER) {
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
            logger_write_pos(logger, __FILE__, __LINE__,
                    "Bad parameter name (\"%s\").", get_token());
#endif
            return 1;
        }
        name = get_token();

        /* What type of key is it? */
        if (strcasecmp(name, "term") == 0)
            key = TERM;
        else if (strcasecmp(name, "cterm") == 0)
            key = CTERM;
        else if (strcasecmp(name, "ctermfg") == 0)
            key = FG;
        else if (strcasecmp(name, "ctermbg") == 0)
            key = BG;
        else
            key = IGNORE;

        /* A '=' must come next. */
        if (yylex() != '=') {
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
            logger_write_pos(logger, __FILE__, __LINE__,
                    "Missing '=' in \"highlight\" command.");
#endif
            return 1;
        }

        /* Process the settings. */
        token = yylex();
        switch (key) {
            case TERM:
            case CTERM:
                attrs = 0;
                while (1) {
                    /* Add in the attribute. */
                    if (token != IDENTIFIER) {
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
                        logger_write_pos(logger, __FILE__, __LINE__,
                                "Bad attribute name: \"%s\".", get_token());
#endif
                        return 1;
                    }
                    name = get_token();

                    pair = lookup_attr_pair_by_name(name);
                    if (!pair) {
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
                        logger_write_pos(logger, __FILE__, __LINE__,
                                "Unknown attribute name");
#endif
                        return 1;
                    }
                    if (pair->value == -1) {
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
                        logger_write_pos(logger, __FILE__, __LINE__,
                                "Unknown attribute name: \"%s\".", name);
#endif
                        return 1;
                    }
                    attrs |= pair->value;

                    /* Are there more attributes? */
                    token = yylex();
                    if (token != ',')
                        break;
                    token = yylex();
                }
                if (key == TERM) {
                    if (mono_attrs == UNSPECIFIED_COLOR)
                        mono_attrs = attrs;
                    else
                        mono_attrs |= attrs;
                } else {
                    if (color_attrs == UNSPECIFIED_COLOR)
                        color_attrs = attrs;
                    else
                        color_attrs |= attrs;
                }
                break;

            case FG:
            case BG:
                attrs = 0;
                switch (token) {
                    case NUMBER:
                        color = atoi(get_token());
                        break;

                    case IDENTIFIER:
                        name = get_token();
                        color_spec = color_spec_for_name(name);
                        if (color_spec == NULL) {
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
                            logger_write_pos(logger, __FILE__, __LINE__,
                                    "Unknown color: \"%s\".", name);
#endif
                            return 1;
                        }
                        color = color_spec->nr8Color;
                        if (color_spec->nr8ForegroundBold) {
                            if (color_attrs == UNSPECIFIED_COLOR)
                                color_attrs = A_BOLD;
                            else
                                color_attrs |= A_BOLD;
                        }
                        break;

                    default:
#ifdef LOG_HIGHLIGHT_COMMAND_ERRORS
                        logger_write_pos(logger, __FILE__, __LINE__,
                                "Bad token for color (\"%s\").", get_token());
#endif
                        return 1;
                }
                if (key == FG)
                    fg_color = color;
                else
                    bg_color = color;
                token = yylex();
                break;

            case IGNORE:
            default:
                /* Ignore the value(s) (potentially a comma-separated list). */
                while (1) {
                    if (token != IDENTIFIER && token != NUMBER)
                        return 1;
                    token = yylex();
                    if (token != ',')
                        break;
                }
                break;
        }
    }

    val = setup_group(hl_groups, group_kind, mono_attrs, color_attrs, fg_color,
            bg_color);
    if (val == -1) {
        return 1;
    }

    return 0;
}

/* Given 24-bit rgb value, calculate closest color in our 16 entry color table */
static int ansi_get_closest_color_value(int r, int g, int b)
{
    static const struct
    {
        int r, g, b;
    } standard_ansi_colors[] = {
      // standard colors
      {   0,   0,   0 }, // COLOR_BLACK
      { 224,   0,   0 }, // COLOR_RED
      {   0, 224,   0 }, // COLOR_GREEN
      { 224, 224,   0 }, // COLOR_YELLOW
      {   0,   0, 224 }, // COLOR_BLUE
      { 224,   0, 224 }, // COLOR_MAGENTA
      {   0, 224, 224 }, // COLOR_CYAN
      { 224, 224, 224 }, // COLOR_WHITE
      // High intensity colors
      { 128, 128, 128 }, // COLOR_BLACK
      { 255,  64,  64 }, // COLOR_RED
      {  64, 255,  64 }, // COLOR_GREEN
      { 255, 255,  64 }, // COLOR_YELLOW
      {  64,  64, 255 }, // COLOR_BLUE
      { 255,  64, 255 }, // COLOR_MAGENTA
      {  64, 255, 255 }, // COLOR_CYAN
      { 255, 255, 255 }, // COLOR_WHITE
    };
    int i;
    int index = 0;
    int distance = -1;

    for (i = 0; i < sizeof(standard_ansi_colors) / sizeof(standard_ansi_colors[0]); i++)
    {
        int r2 = standard_ansi_colors[i].r;
        int g2 = standard_ansi_colors[i].g;
        int b2 = standard_ansi_colors[i].b;
        /* Distance in rgb space. Not all that accurate, but should work for this. */
        int d = (r2-r)*(r2-r) + (g2-g)*(g2-g) + (b2-b)*(b2-b);

        if ((distance == -1) || (d < distance))
        {
            distance = d;
            index = i;
        }
    }

    return index;
}

/*
   In 256-color mode, the color-codes are the following:

   0x00-0x07: standard colors (as in ESC [ 30�37 m)
   0x08-0x0F: high intensity colors (as in ESC [ 90�97 m)
   0x10-0xE7: 6 � 6 � 6 = 216 colors: 16 + 36 � r + 6 � g + b (0 ? r, g, b ? 5)
   0xE8-0xFF: grayscale from black to white in 24 steps

   Set the foreground color to index N: \033[38;5;${N}m
   Set the background color to index M: \033[48;5;${M}m
*/
static int ansi_get_color_code_index(const char *buf, int *index)
{
    int i = 0;

    if (buf[i] == ';' && buf[i+1] == '5' && buf[i+2] == ';')
    {
        int num = 0;

        i += 3;
        while (isdigit(buf[i]))
        {
            num = num * 10 + buf[i] - '0';
            i++;
        }

        if (num >= 232) {
            /* Convert grayscale 232 - 255 value to 0 - 255 rgb value */
            int gray = 255 * (MIN(num, 255) - 232) / (255 - 232);
            num = ansi_get_closest_color_value( gray, gray, gray );
        } else if (num >= 16) {
            /* Convert 0-6 component values to 0 - 255 rgb values */
            int red = ((num - 16) / 36);
            int green = (((num - 16) - red * 36) / 6);
            int blue = ((num - 16) % 6);
            num = ansi_get_closest_color_value( red * 255 / 6, green * 255 / 6, blue * 255 / 6 );
        }

        *index = num;
        return i;
    }

    *index = -1;
    return 0;
}

/* Parse ansi color escape sequence in buf, return ncurses attribute and esc length */
int hl_ansi_get_color_attrs(hl_groups_ptr hl_groups, const char *buf, int *attr)
{
    int i = 0;
    int fg = -1;
    int bg = -1;
    int a = A_NORMAL;

    *attr = 0;

    /* If we're not in ansi mode, just return default color pair 0 and don't parse
       the string. */
    if (!hl_groups->ansi_color)
        return 0;

    if ((buf[i++] == '\033') && (buf[i++] == '[')) {

        /* Check for reset attributes. Ie: \033[m or \033[0m */
        if (buf[i] == 'm')
            return 3;
        else if (buf[i] == '0' && buf[i+1] == 'm')
            return 4;

        /* Parse number;number;number;m sequences */
        for (;;)
        {
            int num = 0;

            /* Should have a number here */
            if (!isdigit(buf[i]))
                return 0;

            while (isdigit(buf[i]))
            {
                num = num * 10 + buf[i] - '0';
                i++;
            }

            /* https://conemu.github.io/en/AnsiEscapeCodes.html#SGR_Select_Graphic_Rendition_parameters */
            switch(num)
            {
            case 0: /* Reset current attributes */
                a = A_NORMAL;
                fg = -1;
                bg = -1;
                break;
            case 1: /* Set BrightOrBold */
                a |= A_BOLD;
                break;
            case 2: /* Unset BrightOrBold */
            case 22: /* Unset BrightOrBold */
                a &= ~A_BOLD;
                break;
            case 4: /* SetBackOrUnderline */
            case 5: /* SetBackOrUnderline */
                a |= A_UNDERLINE;
                break;
            case 3: /* SetItalicOrInverse */
            case 7: /* Use inverse colors */
                a |= A_REVERSE;
                break;
            case 23: /*Unset ItalicOrInverse */
                a &= ~A_REVERSE;
                break;
            case 24: /* UnsetBackOrUnderline */
                a &= ~A_UNDERLINE;
                break;
            case 27: /* Use normal colors */
            case 39: /* Reset text color to defaults */
                fg = -1;
                bg = -1;
                break;
            case 49: /* Reset background color to defaults */
                bg = -1;
                break;
            case 38:
                /* Foreground xterm color code index */
                i += ansi_get_color_code_index(buf + i, &num);
                if (num >= 0 && num < 16) {
                    fg = num & 7;
                    a |= ((num & 0x8) ? A_BOLD : 0);
                } else {
                    a |= A_REVERSE | A_BOLD;
                }
                break;
            case 48:
                /* Background xterm color code index */
                i += ansi_get_color_code_index(buf + i, &num);
                if (num >= 0 && num < 16) {
                    bg = num & 7;
                    a |= ((num & 0x8) ? A_BOLD : 0);
                }
                else {
                    a |= A_REVERSE | A_BOLD;
                }
                break;
                /* Set ANSI text color */
            case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
                fg = num - 30;
                break;
                /* Set ANSI background color */
            case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
                bg = num - 40;
                break;
                /* Set bright ANSI text color */
            case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97:
                fg = num - 90;
                a |= A_BOLD;
                break;
                /* Set bright ANSI background color */
            case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
                bg = num - 100;
                a |= A_BOLD;
                break;
            }

            if (buf[i] == 'm')
            {
                int color_pair = hl_get_color_pair(hl_groups, bg, fg);

                *attr = a | COLOR_PAIR(color_pair);
                return i + 1;
            }

            if (buf[i] != ';')
                return 0;
            i++;
        }
    }

    return 0;
}

/*@}*/
/* }}}*/
