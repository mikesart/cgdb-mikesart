#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if HAVE_STDIO_H
#include <stdio.h>
#endif /* HAVE_STDIO_H */

#if HAVE_STDARG_H
#include <stdarg.h>
#endif /* HAVE_STDARG_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if HAVE_CTYPE_H
#include <ctype.h>
#endif

#if HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */

#include "sys_util.h"

void *cgdb_calloc(size_t nmemb, size_t size)
{
    void *t = calloc(nmemb, size);

    if (t)
        return t;

    exit(-1);
}

void *cgdb_malloc(size_t size)
{
    void *t = malloc(size);

    if (t)
        return t;

    exit(-1);
}

void *cgdb_realloc(void *ptr, size_t size)
{
    void *t = realloc(ptr, size);

    if (t)
        return t;

    exit(-1);
}

char *cgdb_strdup(const char *s)
{
    char *t = strdup(s);

    if (t)
        return t;

    exit(-1);
}

int cgdb_close(int fd)
{
    int ret;

  cgdb_close_start:
    if ((ret = close(fd)) == -1 && errno == EINTR)
        goto cgdb_close_start;
    else if (ret == -1)
        exit(-1);

    return 0;
}

#if defined( __APPLE__ )

// https://developer.apple.com/library/mac/qa/qa1361/_index.html

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/sysctl.h>

static bool AmIBeingDebugged(void)
    // Returns true if the current process is being debugged (either 
    // running under the debugger or has a debugger attached post facto).
{
    int                 junk;
    int                 mib[4];
    struct kinfo_proc   info;
    size_t              size;

    // Initialize the flags so that, if sysctl fails for some bizarre 
    // reason, we get a predictable result.

    info.kp_proc.p_flag = 0;

    // Initialize mib, which tells sysctl the info we want, in this case
    // we're looking for information about a specific process ID.

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    // Call sysctl.

    size = sizeof(info);
    junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    assert(junk == 0);

    // We're being debugged if the P_TRACED flag is set.

    return ( (info.kp_proc.p_flag & P_TRACED) != 0 );
}

#endif // __APPLE__

int cgdb_is_debugger_attached()
{
#ifdef HAVE_PROC_SELF_STATUS_FILE
    int debugger_attached = 0;
    static const char TracerPid[] = "TracerPid:";

    FILE *fp = fopen("/proc/self/status", "r");
    if ( fp ) {
        ssize_t chars_read;
        size_t line_len = 0;
        char *line = NULL;

        while ((chars_read = getline(&line, &line_len, fp)) != -1) {
            char *tracer_pid = strstr(line, TracerPid);

            if (tracer_pid) {
                debugger_attached = !!atoi(tracer_pid + sizeof(TracerPid) - 1);
                break;
            }
        }

        free(line);
        fclose(fp);
    }

    return debugger_attached;
#elif defined( __APPLE__ )
    return AmIBeingDebugged();
#else
    //$ TODO
    return -1;
#endif
}

int log10_uint(unsigned int val)
{
    if (val >= 1000000000u) return 9;
    if (val >= 100000000u) return 8;
    if (val >= 10000000u) return 7;
    if (val >= 1000000u) return 6;
    if (val >= 100000u) return 5;
    if (val >= 10000u) return 4;
    if (val >= 1000u) return 3;
    if (val >= 100u) return 2;
    if (val >= 10u) return 1;
    return 0;
}

/* stb__sbgrowf: internal stretchy buffer grow function.
 */
int stb__sbgrowf( void **arr, int increment, int itemsize )
{
    int m = *arr ? 2 * stb__sbm( *arr ) + increment : increment + 1;
    void *p = cgdb_realloc( *arr ? stb__sbraw( *arr ) : 0,
                            itemsize * m + sizeof( int ) * 2 );

    if ( !*arr )
        ( ( int * )p )[ 1 ] = 0;
    *arr = ( void * )( ( int * )p + 2 );
    stb__sbm( *arr ) = m;

    return 0;
}

char *sys_aprintf(const char *fmt, ...)
{
    int n;
    va_list ap;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap) + 1;
    va_end(ap);

    if (n > 0 ) {
        char *str = (char *)cgdb_malloc(n);

        va_start(ap, fmt);
        vsnprintf(str, n, fmt, ap);
        va_end(ap);

        return str;
    }

    return NULL;
}

uint64_t sys_hexstr_to_u64(const char *line)
{
    char *end;
    uint64_t val = 0;

    while (isspace(*line))
        line++;

    if (line[0] == '0' && line[1] == 'x')
        val = strtoull(line, &end, 16);

    return val;
}
