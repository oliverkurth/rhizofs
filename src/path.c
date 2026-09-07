#include "path.h"

#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>

#include "dbg.h"

#define PATH_SEP '/'

int
path_join(const char * path1, const char * path2, char ** pathjoined)
{
    check((path1 != NULL), "path_join: path1 argument is NULL");
    check((path2 != NULL), "path_join: path2 argument is NULL");
    debug("Joining paths %s and %s", path1, path2);

    size_t lenpath1 = strlen(path1);
    const char * tail = path2;
    size_t add_seperator = 0;

    if (lenpath1 != 0) {
        if (path1[lenpath1-1] == PATH_SEP) {
            /* path1 already ends in a separator, so skip the leading
             * separators of path2 instead of keeping both of them.
             * the previous version sized the buffer as if one of the two
             * separators was dropped, but then still copied path2 in
             * full - writing one byte past the allocation. realpath()
             * returns "/" for the root directory, so a server sharing
             * "/" hit this on every single request. */
            while (*tail == PATH_SEP) {
                ++tail;
            }
        }
        else if (*tail != PATH_SEP) {
            add_seperator = 1;
        }
    }

    size_t lentail = strlen(tail);
    size_t lenpathjoined = lenpath1 + add_seperator + lentail;

    *pathjoined = calloc(lenpathjoined+1, sizeof(char));
    check_mem(*pathjoined);

    memcpy(*pathjoined, path1, lenpath1);
    if (add_seperator == 1) {
        (*pathjoined)[lenpath1] = PATH_SEP;
    }
    memcpy((*pathjoined) + lenpath1 + add_seperator, tail, lentail);

    return 0;


error:
    return -1;
}



bool
path_has_parent_reference(const char * path)
{
    if (path == NULL) {
        return false;
    }

    const char * component_start = path;
    const char * p = path;

    while (1) {
        if (*p == PATH_SEP || *p == '\0') {
            if ((p - component_start) == 2 &&
                    component_start[0] == '.' && component_start[1] == '.') {
                return true;
            }
            if (*p == '\0') {
                break;
            }
            component_start = p + 1;
        }
        ++p;
    }

    return false;
}


int
path_join_real(const char * path1, const char * path2, char ** pathjoined)
{
    char * realp = NULL;

    check((path_join(path1, path2, &realp) == 0), "path_join failed");

    (*pathjoined) = realpath(realp, NULL);
    check(((*pathjoined) != NULL), "realpath failed");

    free(realp);
    return 0;

error:
    free(realp);
    return -1;
}


bool
path_is_within(const char * directory, const char * path)
{
    if ((directory == NULL) || (path == NULL)) {
        return false;
    }

    size_t dirlen = strlen(directory);

    if (strncmp(path, directory, dirlen) != 0) {
        return false;
    }

    /* the served directory itself */
    if (path[dirlen] == '\0') {
        return true;
    }

    /* something below it. the separator has to be there, so that a
     * served directory of "/srv/share" does not also match a sibling
     * called "/srv/shareother" */
    if (path[dirlen] == PATH_SEP) {
        return true;
    }

    /* realpath() only returns a trailing separator for the root
     * directory, which every absolute path is below */
    if ((dirlen != 0) && (directory[dirlen-1] == PATH_SEP)) {
        return true;
    }

    return false;
}


bool
path_parent_resolves_within(const char * directory, const char * path)
{
    char * parent = NULL;
    char * resolved = NULL;
    bool contained = false;

    if ((directory == NULL) || (path == NULL)) {
        return false;
    }

    /* the root of the served directory is contained in itself, while
     * its parent - the directory the share was created in - is not, so
     * it has to be handled before looking at the parent below.
     *
     * this is deliberately a string comparison and not a realpath():
     * resolving the final component would follow a symlink, and one
     * pointing into the mount of the very client being answered sends
     * this worker thread straight back into that client. paths reach
     * this function joined onto the already resolved served directory,
     * so comparing them is enough. */
    size_t pathlen = strlen(path);
    while ((pathlen > 1) && (path[pathlen-1] == PATH_SEP)) {
        --pathlen;
    }
    if ((strlen(directory) == pathlen) &&
            (strncmp(directory, path, pathlen) == 0)) {
        return true;
    }

    parent = path_dirname(path);
    if (parent == NULL) {
        return false;
    }

    resolved = realpath(parent, NULL);
    if (resolved != NULL) {
        contained = path_is_within(directory, resolved);
        free(resolved);
    }

    free(parent);
    return contained;
}


bool
path_resolves_within(const char * directory, const char * path)
{
    char * resolved = NULL;
    bool contained = false;
    struct stat sb;

    if ((directory == NULL) || (path == NULL)) {
        return false;
    }

    resolved = realpath(path, NULL);
    if (resolved != NULL) {
        contained = path_is_within(directory, resolved);
        free(resolved);
        return contained;
    }

    /* anything other than a missing path (a symlink loop, a component
     * that is not a directory, ...) is refused here. the operation
     * itself would have failed on it anyway. */
    if (errno != ENOENT) {
        return false;
    }

    /* the path does not exist, but it still exists as a symlink: a
     * dangling one, whose target realpath() could not resolve. an
     * operation creating the target would follow it, so refuse it
     * instead of only looking at the directory the link lives in. */
    if (lstat(path, &sb) == 0) {
        return false;
    }

    /* the path really does not exist yet - operations creating a new
     * entry are legitimate, so the directory it would be created in is
     * what has to be contained */
    return path_parent_resolves_within(directory, path);
}


char *
path_basename(const char * inpath)
{
    char * inpath_copy = NULL;
    char * return_path = NULL;

    check_debug(inpath != NULL, "inpath is null");

    // make a copy of the argument as basename may modify its parameters
    inpath_copy = strdup(inpath);
    check_mem(inpath_copy);

    return_path = strdup(basename(inpath_copy));
    check_mem(return_path);

    free(inpath_copy);
    return return_path;

error:
    free(inpath_copy);
    free(return_path);
    return NULL;
}


char *
path_dirname(const char * inpath)
{
    char * inpath_copy = NULL;
    char * return_path = NULL;

    check_debug(inpath != NULL, "inpath is null");

    // make a copy of the argument as dirname may modify its parameters
    inpath_copy = strdup(inpath);
    check_mem(inpath_copy);

    return_path = strdup(dirname(inpath_copy));
    check_mem(return_path);

    free(inpath_copy);
    return return_path;

error:
    free(inpath_copy);
    free(return_path);
    return NULL;
}
