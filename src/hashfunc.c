#include "hashfunc.h"


uint64_t
Hashfunc_djb2(const unsigned char * str)
{
    uint64_t hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}


uint64_t
Hashfunc_sdbm(const unsigned char *str)
{
    uint64_t hash = 0;
    int c;

    while ((c = *str++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}
