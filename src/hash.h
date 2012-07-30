#ifndef __hash_h__
#define __hash_h__

#include <stdint.h>

/**
 * various hash functions
 */


/**
 * dan bernstein hasing algorithm
 *
 * see http://www.cse.yorku.ca/~oz/hash.html
 */
uint32_t Hash_djb2(const unsigned char * str);

/**
 * hash algorithm of the sdbm database library
 *
 * good general hashing algorithm with good distribution
 *
 * see http://www.cse.yorku.ca/~oz/hash.html
 */
uint32_t Hash_sdbm(const unsigned char *str);

#endif // __hash_h__
