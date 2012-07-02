#ifndef __dbg_h__
#define __dbg_h__

#include <stdio.h>
#include <errno.h>
#include <string.h>


#define log_print(LEVEL, M, ...) dbg_print(LEVEL, "[%s] " M "\n", dbg_level_string(LEVEL), ##__VA_ARGS__)

#ifdef DEBUG
#define debug(M, ...) log_print(DBG_DEBUG, "%s:%d: " M , __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define debug(M, ...)
#endif

#define check_debug(A, M, ...) if(!(A)) { \
    debug(M, ##__VA_ARGS__); \
    errno=0; \
    goto error; \
}

#define clean_errno() (errno == 0 ? "None" : strerror(errno))

#ifdef DEBUG
/* also print line number */
#define log_err(M, ...) dbg_print(DBG_ERROR, "(%s:%d: errno: %s) " M, __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
#define log_warn(M, ...) dbg_print(DBG_WARN, "(%s:%d: errno: %s) " M, __LINE__, clean_errno(), ##__VA_ARGS__)
#define log_info(M, ...) dbg_print(DBG_INFO, "(%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__)
#else
/* without line number and file */
#define log_err(M, ...) dbg_print(DBG_ERROR, "(errno: %s) " M, clean_errno(), ##__VA_ARGS__)
#define log_warn(M, ...) dbg_print(DBG_WARN, "(errno: %s) " M, clean_errno(), ##__VA_ARGS__)
#define log_info(M, ...) dbg_print(DBG_INFO, "" M, ##__VA_ARGS__)
#endif

#define check(A, M, ...) if(!(A)) { \
    log_err(M, ##__VA_ARGS__); \
    errno=0; \
    goto error; \
}

#define log_and_error(M, ...)  { \
    log_err(M, ##__VA_ARGS__); \
    errno=0; \
    goto error; \
}

#define check_mem(A) check((A), "Out of memory.")

/* macro to hide unused parameter warnings in some cases */
#if defined(__GNUC__)
# define UNUSED_PARAMETER(x)  x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED_PARAMETER(x) /*@unused@*/ x
#else
# define UNUSED_PARAMETER(x) x
#endif

typedef enum _DBG_LEVEL {
    DBG_DEBUG = 0,
    DBG_INFO = 1,
    DBG_WARN = 2,
    DBG_ERROR = 3
} DBG_LEVEL;


void dbg_set_logfile(FILE * file);
void dbg_set_loglevel(const DBG_LEVEL level); 
void dbg_disable_logfile();
void dbg_enable_syslog();
void dbg_disable_syslog();
const char * dbg_level_string(const DBG_LEVEL level);
void dbg_print(const DBG_LEVEL level, const char * fmtstr, ...);

#endif /* __dbg_h__ */
