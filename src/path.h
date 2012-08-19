#ifndef __util_path_h__
#define __util_path_h__

#include <string.h>
#include <stdlib.h>


/**
 * join two paths
 *
 * p1: first path
 * p2: second path
 * pjoined: pointer to the string to write the joined path to
 *     memory will be allocated
 *
 * returns 0 on success
 */
int path_join(const char *, const char *, char **);

int path_join_real(const char *, const char *, char **);

/**
 * return the basename of the path
 *
 * this function, in contrary to the libc functions,
 * will not modify its arguments
 *
 * returns NULL on error, returns a newly allocated
 * string on success. the caller is responsible for freeing this
 * string
 */
char * path_basename(const char * inpath);

#endif /* __util_path_h__ */
