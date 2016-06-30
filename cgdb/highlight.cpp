/* highlight.c:
 * ------------
 * 
 * Syntax highlighting routines.
 *
 */
#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_REGEX_H
#include <regex.h>
#endif /* HAVE_REGEX_H */

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

/* Local Includes */
#include "cgdb.h"
#include "sys_util.h"
#include "highlight.h"
#include "sources.h"
#include "highlight_groups.h"

/* ----------- */
/* Definitions */
/* ----------- */

#define HL_CHAR CHAR_MAX        /* Special marker character */

/* --------------- */
/* Local Variables */
/* --------------- */


/* --------- */
/* Functions */
/* --------- */

struct hl_regex_info {
    regex_t t;
    int icase;
    char *regex;
};

const char *hl_regex_get(struct hl_regex_info *info)
{
    if (info && info->regex && info->regex[0])
        return info->regex;

    return NULL;
}

void hl_regex_free(struct hl_regex_info **info)
{
    if (info && *info && (*info)->regex) {
        regfree(&(*info)->t);

        free((*info)->regex);
        (*info)->regex = NULL;

        free(*info);
        *info = NULL;
    }
}

int hl_regex_search(struct hl_regex_info **info, char *line, const char *regex, int icase, int *start, int *end)
{
    int result;
    regmatch_t pmatch;
    int recompile = 0;

    *start = -1;
    *end = -1;

    if (!regex || !regex[0])
        return -1;
    
    if (!*info) {
        *info = (struct hl_regex_info *)cgdb_calloc(1, sizeof(struct hl_regex_info));
        recompile = 1;
    } 
    else if (!(*info)->regex)
        recompile = 1;
    else if ((*info)->regex == regex)
        recompile = 0;
    else if ((icase != -1) && (icase != (*info)->icase))
        recompile = 1;
    else if (strcmp(regex, (*info)->regex))
        recompile = 1;

    if (recompile) {
        if (*info && (*info)->regex) {
            regfree(&(*info)->t);

            free((*info)->regex);
            (*info)->regex = NULL;
        }

        /* Compile the regular expression */
        if (regcomp(&(*info)->t, regex, REG_EXTENDED | (icase ? REG_ICASE : 0)) != 0) {
            hl_regex_free(info);
            return -1;
        }

        (*info)->regex = strdup(regex);
        (*info)->icase = icase;
    }

    char *lf = strchr(line, '\n');
    if (lf)
        *lf = '\0';

    result = regexec(&(*info)->t, line, 1, &pmatch, 0);

    if (lf)
        *lf = '\n';

    if ((result == 0) && (pmatch.rm_eo > pmatch.rm_so)) {
        *start = pmatch.rm_so;
        *end = pmatch.rm_eo;
        return 1;
    }

    return 0;
}

struct hl_line_attr *hl_regex_highlight(struct hl_regex_info **info, char *line)
{
    hl_line_attr *attrs = NULL;

    if (*info && (*info)->regex && (*info)->regex[0]) {
        int pos = 0;
        int search_attr;
        struct hl_line_attr line_attr;

        hl_groups_get_attr(hl_groups_instance, HLG_SEARCH, &search_attr);

        for (;;) {
            int ret;
            int len;
            int start, end;

            ret = hl_regex_search(info, line + pos, (*info)->regex, (*info)->icase, &start, &end);
            if (ret <= 0)
                break;

            len = end - start;
            pos += start;

            /* Push search attribute */
            line_attr.attr = search_attr;
            line_attr.col = pos;
            sbpush(attrs, line_attr);

            /* And the back to regular text attribute */
            line_attr.attr = 0;
            line_attr.col = pos + len;
            sbpush(attrs, line_attr);

            pos += len;
        }
    }

    return attrs;
}
