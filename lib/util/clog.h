/* clog: Extremely simple logger for C.
 *
 * Features:
 * - Implemented purely as a single header file.
 * - Create multiple loggers.
 * - Four log levels (debug, info, warn, error).
 * - Custom formats.
 * - Fast.
 *
 * Dependencies:
 * - Should conform to C89, C++98 (but requires vsnprintf, unfortunately).
 * - POSIX environment.
 *
 * USAGE:
 *
 * Include this header in any file that wishes to write to logger(s).  In
 * exactly one file (per executable), define CLOG_MAIN first (e.g. in your
 * main .c file).
 *
 *     #define CLOG_MAIN
 *     #include "clog.h"
 *
 * This will define the actual objects that all the other units will use.
 *
 * Loggers are identified by integers (0 - 15).  It's expected that you'll
 * create meaningful constants and then refer to the loggers as such.
 *
 * Example:
 *
 *  const int MY_LOGGER = 0;
 *
 *  int main() {
 *      int r;
 *      r = clog_init_path(MY_LOGGER, "my_log.txt");
 *      if (r != 0) {
 *          fprintf(stderr, "Logger initialization failed.\n");
 *          return 1;
 *      }
 *      clog_info(CLOG(MY_LOGGER), "Hello, world!");
 *      clog_free(MY_LOGGER);
 *      return 0;
 *  }
 *
 * The CLOG macro used in the call to clog_info is a helper that passes the
 * __FILE__ and __LINE__ parameters for you, so you don't have to type them
 * every time. (It could be prettier with variadic macros, but that requires
 * C99 or C++11 to be standards compliant.)
 *
 * Errors encountered by clog will be printed to stderr.  You can suppress
 * these by defining a macro called CLOG_SILENT before including clog.h.
 *
 * License: Do whatever you want. It would be nice if you contribute
 * improvements as pull requests here:
 *
 *   https://github.com/mmueller/clog
 *
 * Copyright 2013 Mike Mueller <mike@subfocal.net>.
 *
 * As is; no warranty is provided; use at your own risk.
 */

#ifndef __CLOG_H__
#define __CLOG_H__

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>

/* Number of loggers that can be defined. */
#define CLOG_MAX_LOGGERS 16

/* cgdb clog IDs */
/*   1: cgdb logging */
#define CLOG_CGDB_ID 1
#define CLOG_CGDB CLOG(CLOG_CGDB_ID)
/*   2: gdb io logging (io.cpp) */
#define CLOG_GDBIO_ID 2
#define CLOG_GDBIO CLOG(CLOG_GDBIO_ID)

/* Format strings cannot be longer than this. */
#define CLOG_FORMAT_LENGTH 256

/* Formatted times and dates should be less than this length. If they are not,
 * they will not appear in the log. */
#define CLOG_DATETIME_LENGTH 256

/* Default format strings. */
/* #define CLOG_DEFAULT_FORMAT "%d %t %f(%n): %l: %m\n" */
#define CLOG_DEFAULT_FORMAT "%d %t %f:%n(%F) %l:%m\n\n"
#define CLOG_DEFAULT_DATE_FORMAT "%Y-%m-%d"
#define CLOG_DEFAULT_TIME_FORMAT "%H:%M:%S"

#ifdef __cplusplus
extern "C" {
#endif

enum clog_level
{
    CLOG_DEBUG,
    CLOG_INFO,
    CLOG_WARN,
    CLOG_ERROR
};

struct clog;

/**
 * Create a new logger writing to the given file path.  The file will always
 * be opened in append mode.
 *
 * @param id
 * A constant integer between 0 and 15 that uniquely identifies this logger.
 *
 * @param path
 * Path to the file where log messages will be written.
 *
 * @return
 * Zero on success, non-zero on failure.
 */
int clog_init_path( int id, const char *const path );

/**
 * Create a new logger writing to a file descriptor.
 *
 * @param id
 * A constant integer between 0 and 15 that uniquely identifies this logger.
 *
 * @param fd
 * The file descriptor where log messages will be written.
 *
 * @return
 * Zero on success, non-zero on failure.
 */
int clog_init_fd( int id, int fd );

/**
 * Destroy (clean up) a logger.  You should do this at the end of execution,
 * or when you are done using the logger.
 *
 * @param id
 * The id of the logger to destroy.
 */
void clog_free( int id );

#define CLOG( id ) __FILE__, __LINE__, __FUNCTION__, id

/**
 * Log functions (one per level).  Call these to write messages to the log
 * file.  The first three arguments can be replaced with a call to the CLOG
 * macro defined above, e.g.:
 *
 *     clog_debug(CLOG(MY_LOGGER_ID), "This is a log message.");
 *
 * @param sfile
 * The name of the source file making this log call (e.g. __FILE__).
 *
 * @param sline
 * The line number of the call in the source code (e.g. __LINE__).
 *
 * @param id
 * The id of the logger to write to.
 *
 * @param fmt
 * The format string for the message (printf formatting).
 *
 * @param ...
 * Any additional format arguments.
 */
void clog_debug( const char *sfile, int sline, const char *sfunc, int id, const char *fmt, ... ) ATTRIBUTE_PRINTF(5, 6);
void clog_info( const char *sfile, int sline, const char *sfunc, int id, const char *fmt, ... ) ATTRIBUTE_PRINTF(5, 6);
void clog_warn( const char *sfile, int sline, const char *sfunc, int id, const char *fmt, ... ) ATTRIBUTE_PRINTF(5, 6);
void clog_error( const char *sfile, int sline, const char *sfunc, int id, const char *fmt, ... ) ATTRIBUTE_PRINTF(5, 6);

/**
 * Set the minimum level of messages that should be written to the log.
 * Messages below this level will not be written.  By default, loggers are
 * created with level == CLOG_DEBUG.
 *
 * @param id
 * The identifier of the logger.
 *
 * @param level
 * The new minimum log level.
 *
 * @return
 * Zero on success, non-zero on failure.
 */
int clog_set_level( int id, enum clog_level level );

enum clog_level clog_get_level( int id );
char *clog_get_path( int id );
size_t clog_get_byteswritten( int id );
int clog_set_echo_to_stderr( int id, int echo_to_stderr );

/**
 * Set the format string used for times.  See strftime(3) for how this string
 * should be defined.  The default format string is CLOG_DEFAULT_TIME_FORMAT.
 *
 * @param fmt
 * The new format string, which must be less than CLOG_FORMAT_LENGTH bytes.
 *
 * @return
 * Zero on success, non-zero on failure.
 */
int clog_set_time_fmt( int id, const char *fmt );

/**
 * Set the format string used for dates.  See strftime(3) for how this string
 * should be defined.  The default format string is CLOG_DEFAULT_DATE_FORMAT.
 *
 * @param fmt
 * The new format string, which must be less than CLOG_FORMAT_LENGTH bytes.
 *
 * @return
 * Zero on success, non-zero on failure.
 */
int clog_set_date_fmt( int id, const char *fmt );

/**
 * Set the format string for log messages.  Here are the substitutions you may
 * use:
 *
 *     %f: Source file name generating the log call.
 *     %n: Source line number where the log call was made.
 *     %m: The message text sent to the logger (after printf formatting).
 *     %d: The current date, formatted using the logger's date format.
 *     %t: The current time, formatted using the logger's time format.
 *     %l: The log level (one of "DEBUG", "INFO", "WARN", or "ERROR").
 *     %%: A literal percent sign.
 *
 * The default format string is CLOG_DEFAULT_FORMAT.
 *
 * @param fmt
 * The new format string, which must be less than CLOG_FORMAT_LENGTH bytes.
 * You probably will want to end this with a newline (\n).
 *
 * @return
 * Zero on success, non-zero on failure.
 */
int clog_set_fmt( int id, const char *fmt );

/*
 * No need to read below this point.
 */

/**
 * The C logger structure.
 */
struct clog
{
    /* The current level of this logger. Messages below it will be dropped. */
    enum clog_level level;

    /* The file being written. */
    int fd;

    /* The format specifier. */
    char fmt[ CLOG_FORMAT_LENGTH ];

    /* Date format */
    char date_fmt[ CLOG_FORMAT_LENGTH ];

    /* Time format */
    char time_fmt[ CLOG_FORMAT_LENGTH ];

    /* Tracks whether the fd needs to be closed eventually. */
    int opened;

    /* Logfile pathname if opened with clog_init_path, otherwise NULL */
    char *pathname;

    /* Count of bytes written to the logfile */
    size_t byteswritten;

    /* Echo output to stderr? */
    int echo_to_stderr;
};

void _clog_err( const char *fmt, ... ) ATTRIBUTE_PRINTF(1, 2);

#ifdef CLOG_MAIN
struct clog *_clog_loggers[ CLOG_MAX_LOGGERS ] = { 0 };
#else
extern struct clog *_clog_loggers[ CLOG_MAX_LOGGERS ];
#endif

#ifdef CLOG_MAIN

const char *const CLOG_LEVEL_NAMES[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
};

int clog_init_path( int id, const char *const path )
{
    int fd = open( path, O_CREAT | O_WRONLY, 0666 );
    if ( fd == -1 )
    {
        _clog_err( "Unable to open %s: %s\n", path, strerror( errno ) );
        return 1;
    }

    int res = flock( fd, LOCK_EX | LOCK_NB );
    if ( res == -1 )
    {
        close( fd );
        return 2;
    }

    ftruncate( fd, 0 );

    if ( clog_init_fd( id, fd ) )
    {
        close( fd );
        return 1;
    }
    _clog_loggers[ id ]->opened = 1;
    _clog_loggers[ id ]->pathname = strdup(path);
    return 0;
}

int clog_init_fd( int id, int fd )
{
    struct clog *clogger;

    if ( _clog_loggers[ id ] != NULL )
    {
        _clog_err( "Logger %d already initialized.\n", id );
        return 1;
    }

    clogger = ( struct clog * )malloc( sizeof( struct clog ) );
    if ( clogger == NULL )
    {
        _clog_err( "Failed to allocate clogger: %s\n", strerror( errno ) );
        return 1;
    }

    clogger->level = CLOG_DEBUG;
    clogger->fd = fd;
    clogger->opened = 0;
    clogger->pathname = NULL;
    clogger->byteswritten = 0;
    clogger->echo_to_stderr = 0;
    strcpy( clogger->fmt, CLOG_DEFAULT_FORMAT );
    strcpy( clogger->date_fmt, CLOG_DEFAULT_DATE_FORMAT );
    strcpy( clogger->time_fmt, CLOG_DEFAULT_TIME_FORMAT );

    _clog_loggers[ id ] = clogger;
    return 0;
}

void clog_free( int id )
{
    if ( _clog_loggers[ id ] )
    {
        if ( _clog_loggers[ id ]->opened )
        {
            close( _clog_loggers[ id ]->fd );
        }

        free( _clog_loggers[ id ]->pathname );

        free( _clog_loggers[ id ] );
        _clog_loggers[ id ] = 0;
    }
}

int clog_set_level( int id, enum clog_level level )
{
    if ( _clog_loggers[ id ] == NULL )
    {
        return 1;
    }
    if ( ( unsigned )level > CLOG_ERROR )
    {
        return 1;
    }
    _clog_loggers[ id ]->level = level;
    return 0;
}

enum clog_level clog_get_level( int id )
{
    if ( _clog_loggers[ id ] == NULL )
    {
        return (enum clog_level)-1;
    }

    return _clog_loggers[ id ]->level;
}

int clog_set_echo_to_stderr( int id, int echo_to_stderr )
{
    if ( _clog_loggers[ id ] == NULL )
    {
        return 1;
    }
    _clog_loggers[ id ]->echo_to_stderr = echo_to_stderr;
    return 0;
}

char *clog_get_path( int id )
{
    if ( _clog_loggers[ id ] == NULL )
    {
        return NULL;
    }

    return _clog_loggers[ id ]->pathname;
}

size_t clog_get_byteswritten( int id )
{
    if ( _clog_loggers[ id ] == NULL )
    {
        return 0;
    }

    return _clog_loggers[ id ]->byteswritten;
}

int clog_set_time_fmt( int id, const char *fmt )
{
    struct clog *clogger = _clog_loggers[ id ];
    if ( clogger == NULL )
    {
        _clog_err( "clog_set_time_fmt: No such clogger: %d\n", id );
        return 1;
    }
    if ( strlen( fmt ) >= CLOG_FORMAT_LENGTH )
    {
        _clog_err( "clog_set_time_fmt: Format specifier too long.\n" );
        return 1;
    }
    strcpy( clogger->time_fmt, fmt );
    return 0;
}

int clog_set_date_fmt( int id, const char *fmt )
{
    struct clog *clogger = _clog_loggers[ id ];
    if ( clogger == NULL )
    {
        _clog_err( "clog_set_date_fmt: No such clogger: %d\n", id );
        return 1;
    }
    if ( strlen( fmt ) >= CLOG_FORMAT_LENGTH )
    {
        _clog_err( "clog_set_date_fmt: Format specifier too long.\n" );
        return 1;
    }
    strcpy( clogger->date_fmt, fmt );
    return 0;
}

int clog_set_fmt( int id, const char *fmt )
{
    struct clog *clogger = _clog_loggers[ id ];
    if ( clogger == NULL )
    {
        _clog_err( "clog_set_fmt: No such clogger: %d\n", id );
        return 1;
    }
    if ( strlen( fmt ) >= CLOG_FORMAT_LENGTH )
    {
        _clog_err( "clog_set_fmt: Format specifier too long.\n" );
        return 1;
    }
    strcpy( clogger->fmt, fmt );
    return 0;
}

/* Internal functions */

size_t
_clog_append_str( char **dst, char *orig_buf, const char *src, size_t cur_size )
{
    size_t new_size = cur_size;

    while ( strlen( *dst ) + strlen( src ) >= new_size )
    {
        new_size *= 2;
    }
    if ( new_size != cur_size )
    {
        if ( *dst == orig_buf )
        {
            *dst = ( char * )malloc( new_size );
            strcpy( *dst, orig_buf );
        }
        else
        {
            *dst = ( char * )realloc( *dst, new_size );
        }
    }

    strcat( *dst, src );
    return new_size;
}

size_t
_clog_append_int( char **dst, char *orig_buf, long int d, size_t cur_size )
{
    char buf[ 40 ]; /* Enough for 128-bit decimal */
    if ( snprintf( buf, 40, "%ld", d ) >= 40 )
    {
        return cur_size;
    }
    return _clog_append_str( dst, orig_buf, buf, cur_size );
}

size_t
_clog_append_time( char **dst, char *orig_buf, struct tm *lt,
                   const char *fmt, size_t cur_size )
{
    char buf[ CLOG_DATETIME_LENGTH ];
    size_t result = strftime( buf, CLOG_DATETIME_LENGTH, fmt, lt );

    if ( result > 0 )
    {
        return _clog_append_str( dst, orig_buf, buf, cur_size );
    }

    return cur_size;
}

const char *
_clog_basename( const char *path )
{
    const char *slash = strrchr( path, '/' );
    if ( slash )
    {
        path = slash + 1;
    }
#ifdef _WIN32
    slash = strrchr( path, '\\' );
    if ( slash )
    {
        path = slash + 1;
    }
#endif
    return path;
}

char *
_clog_format( const struct clog *clogger, char buf[], size_t buf_size,
              const char *sfile, int sline, const char *sfunc, const char *level,
              const char *message )
{
    size_t cur_size = buf_size;
    char *result = buf;
    enum
    {
        NORMAL,
        SUBST
    } state = NORMAL;
    size_t fmtlen = strlen( clogger->fmt );
    size_t i;
    time_t t = time( NULL );
    struct tm *lt = localtime( &t );

    sfile = _clog_basename( sfile );
    result[ 0 ] = 0;
    for ( i = 0; i < fmtlen; ++i )
    {
        if ( state == NORMAL )
        {
            if ( clogger->fmt[ i ] == '%' )
            {
                state = SUBST;
            }
            else
            {
                char str[ 2 ] = { 0 };
                str[ 0 ] = clogger->fmt[ i ];
                cur_size = _clog_append_str( &result, buf, str, cur_size );
            }
        }
        else
        {
            switch ( clogger->fmt[ i ] )
            {
            case '%':
                cur_size = _clog_append_str( &result, buf, "%", cur_size );
                break;
            case 't':
                cur_size = _clog_append_time( &result, buf, lt,
                                              clogger->time_fmt, cur_size );
                break;
            case 'd':
                cur_size = _clog_append_time( &result, buf, lt,
                                              clogger->date_fmt, cur_size );
                break;
            case 'l':
                cur_size = _clog_append_str( &result, buf, level, cur_size );
                break;
            case 'n':
                cur_size = _clog_append_int( &result, buf, sline, cur_size );
                break;
            case 'f':
                cur_size = _clog_append_str( &result, buf, sfile, cur_size );
                break;
            case 'F':
                cur_size = _clog_append_str( &result, buf, sfunc, cur_size );
                break;
            case 'm':
                cur_size = _clog_append_str( &result, buf, message,
                                             cur_size );
                break;
            }
            state = NORMAL;
        }
    }

    return result;
}

void _clog_log( const char *sfile, int sline, const char *sfunc, enum clog_level level,
                int id, const char *fmt, va_list ap ) ATTRIBUTE_PRINTF(6,0);

void _clog_log( const char *sfile, int sline, const char *sfunc, enum clog_level level,
                int id, const char *fmt, va_list ap )
{
    /* For speed: Use a stack buffer until message exceeds 4096, then switch
     * to dynamically allocated.  This should greatly reduce the number of
     * memory allocations (and subsequent fragmentation). */
    char buf[ 4096 ];
    size_t buf_size = 4096;
    char *dynbuf = buf;
    char *message;
    int result;
    struct clog *clogger = _clog_loggers[ id ];

    if ( !clogger )
    {
        _clog_err( "No such clogger: %d\n", id );
        return;
    }

    if ( level < clogger->level )
    {
        return;
    }

    /* Format the message text with the argument list. */
    result = vsnprintf( dynbuf, buf_size, fmt, ap );
    if ( ( size_t )result >= buf_size )
    {
        buf_size = result + 1;
        dynbuf = ( char * )malloc( buf_size );
        result = vsnprintf( dynbuf, buf_size, fmt, ap );
        if ( ( size_t )result >= buf_size )
        {
            /* Formatting failed -- too large */
            _clog_err( "Formatting failed (1).\n" );
            free( dynbuf );
            return;
        }
    }

    /* Format according to log format and write to log */
    {
        char message_buf[ 4096 ];
        message = _clog_format( clogger, message_buf, 4096, sfile, sline, sfunc,
                                CLOG_LEVEL_NAMES[ level ], dynbuf );
        if ( !message )
        {
            _clog_err( "Formatting failed (2).\n" );
            if ( dynbuf != buf )
            {
                free( dynbuf );
            }
            return;
        }

        if ( clogger->echo_to_stderr )
            write( STDERR_FILENO, message, strlen( message ) );

        result = write( clogger->fd, message, strlen( message ) );

        if ( result == -1 )
        {
            _clog_err( "Unable to write to log file: %s\n", strerror( errno ) );
        }
        else
        {
            clogger->byteswritten += result;
        }
        if ( message != message_buf )
        {
            free( message );
        }
        if ( dynbuf != buf )
        {
            free( dynbuf );
        }
    }
}

void clog_debug( const char *sfile, int sline, const char *sfunc, int id, const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    _clog_log( sfile, sline, sfunc, CLOG_DEBUG, id, fmt, ap );
}

void clog_info( const char *sfile, int sline, const char *sfunc, int id, const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    _clog_log( sfile, sline, sfunc, CLOG_INFO, id, fmt, ap );
}

void clog_warn( const char *sfile, int sline, const char *sfunc, int id, const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    _clog_log( sfile, sline, sfunc, CLOG_WARN, id, fmt, ap );
}

void clog_error( const char *sfile, int sline, const char *sfunc, int id, const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    _clog_log( sfile, sline, sfunc, CLOG_ERROR, id, fmt, ap );
}

void _clog_err( const char *fmt, ... )
{
#ifdef CLOG_SILENT
    ( void )fmt;
#else
    va_list ap;

    va_start( ap, fmt );
    vfprintf( stderr, fmt, ap );
#endif
}

#endif /* CLOG_MAIN */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __CLOG_H__ */
