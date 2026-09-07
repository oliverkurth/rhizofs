#ifndef __util_path_h__
#define __util_path_h__

#include <stdbool.h>
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
 * check if a (client-supplied) relative path contains a ".."
 * component which, when joined onto a base directory, could be
 * used to escape it (directory traversal)
 *
 * returns true if the path contains such a component, false otherwise
 */
bool path_has_parent_reference(const char * path);

/**
 * check if "path" is lexically located inside "directory" - both are
 * expected to be absolute and free of any "." or ".." components, as
 * returned by realpath()
 *
 * returns true if path is the directory itself or below it
 */
bool path_is_within(const char * directory, const char * path);

/**
 * check if the directory "path" lives in stays inside "directory" once
 * all symlinks are resolved.
 *
 * use this for operations acting on the entry itself rather than on
 * whatever it may point to (lstat(), readlink(), unlink(), rename(),
 * ...): the final component is not resolved, so a symlink pointing out
 * of "directory" is still reported as contained.
 *
 * returns false if it escapes or cannot be resolved
 */
bool path_parent_resolves_within(const char * directory, const char * path);

/**
 * check if "path" stays inside "directory" once all symlinks are
 * resolved, including one in the final component.
 *
 * use this for operations following the final component (open(),
 * opendir(), truncate(), chmod(), ...). a path that does not exist yet
 * is accepted if the directory it would be created in is contained,
 * but a dangling symlink is refused - an operation creating its target
 * would follow it out of "directory".
 *
 * returns false if it escapes or cannot be resolved
 */
bool path_resolves_within(const char * directory, const char * path);

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

/**
 * return the dirname of the path
 *
 * this function, in contrary to the libc functions,
 * will not modify its arguments
 *
 * returns NULL on error, returns a newly allocated
 * string on success. the caller is responsible for freeing this
 * string
 */
char * path_dirname(const char * inpath);


#endif /* __util_path_h__ */
