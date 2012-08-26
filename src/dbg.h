#ifndef __dbg_h__
#define __dbg_h__

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "helpers.h"

extern FILE *LOG_FILE;

#ifdef DEBUG
#define debug(M, ...) fprintf(LOG_FILE, "[DEBUG] %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
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
#define log_err(M, ...) fprintf(LOG_FILE, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
#define log_warn(M, ...) fprintf(LOG_FILE, "[WARN] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
#define log_info(M, ...) fprintf(LOG_FILE, "[INFO] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
/* without line number and file */
#define log_err(M, ...) fprintf(LOG_FILE, "[ERROR] (errno: %s) " M "\n", clean_errno(), ##__VA_ARGS__)
#define log_warn(M, ...) fprintf(LOG_FILE, "[WARN] (errno: %s) " M "\n", clean_errno(), ##__VA_ARGS__)
#define log_info(M, ...) fprintf(LOG_FILE, "[INFO] " M "\n", ##__VA_ARGS__)
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

#define check_mem(A) check((A), "Out of memory. Could not allocate " STRINGIFY(A))

/* macro to hide unused parameter warnings in some cases */
#if defined(__GNUC__)
# define UNUSED_PARAMETER(x)  x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED_PARAMETER(x) /*@unused@*/ x
#else
# define UNUSED_PARAMETER(x) x
#endif

#endif /* __dbg_h__ */
