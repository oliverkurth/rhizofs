#ifndef _BSD_SOURCE
#define _BSD_SOURCE // for vsyslog
#endif

#include "dbg.h"


#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>

static FILE * log_file = NULL;
static bool use_syslog = false;
static DBG_LEVEL log_level = DBG_DEBUG; 


// names for debug levels as strings. indices have to match
// the DBG_LEVELS enum
static const char * dbg_level_strings[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

// syslog levels for the DBG_LEVELS. indices have to match again
static const int dbg_level_syslog[] = {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERR
};

inline void
dbg_set_logfile(FILE * file)
{
    log_file = file;
}

inline void 
dbg_set_loglevel(const DBG_LEVEL level)
{
    log_level = level;
}

inline void
dbg_disable_logfile() 
{
    log_file = NULL;
}

inline void 
dbg_enable_syslog() 
{
    use_syslog = true;
}

inline void
dbg_disable_syslog()
{
    use_syslog = false;
}

const char *
dbg_level_string(const DBG_LEVEL level)
{
    if (level < (sizeof(dbg_level_strings)/sizeof(const char *))) {
        return dbg_level_strings[level];
    }
    return dbg_level_strings[0];
}

void 
dbg_print(const DBG_LEVEL level, const char * fmtstr, ...)
{
    if (level >= log_level) {
        va_list args;
        if (log_file != NULL) {
            va_start(args, fmtstr);
            vfprintf(log_file, fmtstr, args);
            va_end(args);

            // immediately flush the log file
            // this will not be very perfromant for lots of log
            // messages, but ensures that the logfile gets written
            // the moment something is logged
            fflush(log_file);
        }
        if (use_syslog && (level < (sizeof(dbg_level_syslog)/sizeof(const int)))) {
#ifndef __APPLE__
            va_start(args, fmtstr);
            vsyslog(dbg_level_syslog[level], fmtstr, args);
            va_end(args);
#else
            char buf[1024];
            va_start(args, fmtstr);
            vsnprintf(buf, sizeof(buf), fmtstr, args);
            va_end(args);
            syslog(dbg_level_syslog[level], "%s", buf);
#endif
        }
    }
}
