#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "hashfunc.h"
#include "dbg.h"

uint64_t
Hashfunc_djb2(const unsigned char * str)
{
    uint64_t hash = 5381;
    int c;
#ifdef DEBUG
    const unsigned char * str_start = str;
#endif

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    debug("Hashfunc_djb2(%s) -> %" PRIu64, str_start, hash);
    return hash;
}


uint64_t
Hashfunc_sdbm(const unsigned char *str)
{
    uint64_t hash = 0;
    int c;
#ifdef DEBUG
    const unsigned char * str_start = str;
#endif

    while ((c = *str++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    debug("Hashfunc_sdbm(%s) -> %" PRIu64, str_start, hash);
    return hash;
}
