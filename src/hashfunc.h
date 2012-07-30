#ifndef __hashfunc_h__
#define __hashfunc_h__

#include <stdint.h>

/**
 * various hash functions
 */


/**
 * dan bernstein hasing algorithm
 *
 * see http://www.cse.yorku.ca/~oz/hash.html
 */
uint32_t Hashfunc_djb2(const unsigned char * str);

/**
 * hash algorithm of the sdbm database library
 *
 * good general hashing algorithm with good distribution
 *
 * see http://www.cse.yorku.ca/~oz/hash.html
 */
uint32_t Hashfunc_sdbm(const unsigned char *str);

#endif // __hashfunc_h__
