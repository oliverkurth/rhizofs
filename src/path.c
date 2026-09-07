#include "path.h"

#include <libgen.h>

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
