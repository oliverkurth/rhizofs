/*
 * regression tests for security fixes in code that cannot be reached
 * through the wire protocol from the outside (or only under a race),
 * so that the checks are still exercised by a normal, non-sanitized
 * build.
 *
 * built by "make testtools" and run by pytest/test_security_unit.py.
 *
 * prints one line per failed check and exits with 1 if anything failed,
 * 0 otherwise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/path.h"

static int failures = 0;

static void
check_join(const char * path1, const char * path2, const char * expected)
{
    char * joined = NULL;

    if (path_join(path1, path2, &joined) != 0) {
        fprintf(stderr, "FAIL path_join(\"%s\", \"%s\") returned an error\n",
                path1, path2);
        ++failures;
        return;
    }

    if (strcmp(joined, expected) != 0) {
        fprintf(stderr, "FAIL path_join(\"%s\", \"%s\") = \"%s\", expected \"%s\"\n",
                path1, path2, joined, expected);
        ++failures;
    }

    free(joined);
}

static void
test_path_join(void)
{
    /* the ordinary cases: exactly one separator between the two parts */
    check_join("/srv/share", "/file", "/srv/share/file");
    check_join("/srv/share", "file", "/srv/share/file");
    check_join("/srv/share", "/sub/file", "/srv/share/sub/file");

    /* a first path that already ends in a separator used to be sized as
     * if one of the two separators was dropped while both were still
     * written, running one byte past the allocation. realpath() returns
     * "/" for the root directory, so a server sharing "/" hit this for
     * every request it served. */
    check_join("/", "/file", "/file");
    check_join("/", "/", "/");
    check_join("/srv/share/", "/file", "/srv/share/file");
    check_join("/srv/share/", "file", "/srv/share/file");
    check_join("/srv/share/", "///file", "/srv/share/file");

    /* an empty first path was mis-sized the same way */
    check_join("", "/file", "/file");
    check_join("", "file", "file");
}

int
main(void)
{
    test_path_join();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    printf("all checks passed\n");
    return 0;
}
