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
#include <unistd.h>

#include <sys/stat.h>

#include "../src/path.h"
#include "../src/datablock.h"

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

static void
check_bool(bool got, bool expected, const char * what)
{
    if (got != expected) {
        fprintf(stderr, "FAIL %s: got %s, expected %s\n", what,
                got ? "true" : "false", expected ? "true" : "false");
        ++failures;
    }
}

static void
test_path_containment(void)
{
    /* purely lexical containment */
    check_bool(path_is_within("/srv/share", "/srv/share"), true,
            "path_is_within(share, share)");
    check_bool(path_is_within("/srv/share", "/srv/share/sub/file"), true,
            "path_is_within(share, share/sub/file)");
    check_bool(path_is_within("/srv/share", "/srv/shareother"), false,
            "path_is_within(share, a sibling with the same prefix)");
    check_bool(path_is_within("/srv/share", "/srv"), false,
            "path_is_within(share, its parent)");
    check_bool(path_is_within("/srv/share", "/etc/passwd"), false,
            "path_is_within(share, an unrelated path)");
    /* a served root directory, as realpath() reports it */
    check_bool(path_is_within("/", "/etc/passwd"), true,
            "path_is_within(/, /etc/passwd)");

    /* containment of paths that have to be resolved on the filesystem */
    char tmpl[] = "/tmp/rhizo-security-unit-XXXXXX";
    char * base = mkdtemp(tmpl);
    if (base == NULL) {
        fprintf(stderr, "FAIL could not create a temporary directory\n");
        ++failures;
        return;
    }

    /* the destination buffers are twice the size of the directory ones,
     * so that appending a name can never truncate */
    char share[256], outside[256];
    char path[512];
    snprintf(share, sizeof(share), "%s/share", base);
    snprintf(outside, sizeof(outside), "%s/outside", base);
    mkdir(share, 0755);
    mkdir(outside, 0755);

    snprintf(path, sizeof(path), "%s/inside.txt", share);
    fclose(fopen(path, "w"));

    snprintf(path, sizeof(path), "%s/secret.txt", outside);
    fclose(fopen(path, "w"));

    /* the root of the served directory is contained in itself - its
     * parent is not, so this needs handling of its own */
    check_bool(path_resolves_within(share, share), true,
            "path_resolves_within(share, share)");
    check_bool(path_parent_resolves_within(share, share), true,
            "path_parent_resolves_within(share, share)");
    snprintf(path, sizeof(path), "%s/", share);
    check_bool(path_parent_resolves_within(share, path), true,
            "path_parent_resolves_within(share, share with a trailing sep)");

    /* an ordinary file in the share */
    snprintf(path, sizeof(path), "%s/inside.txt", share);
    check_bool(path_resolves_within(share, path), true,
            "path_resolves_within(share, a file in it)");
    check_bool(path_parent_resolves_within(share, path), true,
            "path_parent_resolves_within(share, a file in it)");

    /* a path that does not exist yet: creating it is legitimate */
    snprintf(path, sizeof(path), "%s/tobecreated.txt", share);
    check_bool(path_resolves_within(share, path), true,
            "path_resolves_within(share, a path to be created in it)");

    /* a symlink in the share pointing out of it must not be followed,
     * but operations acting on the link itself (lstat, readlink) are
     * fine and have to keep working */
    char target[512];
    snprintf(target, sizeof(target), "%s/secret.txt", outside);
    snprintf(path, sizeof(path), "%s/escape", share);
    symlink(target, path);
    check_bool(path_resolves_within(share, path), false,
            "path_resolves_within(share, a symlink pointing out of it)");
    check_bool(path_parent_resolves_within(share, path), true,
            "path_parent_resolves_within(share, a symlink pointing out of it)");

    /* below such a symlink nothing is reachable at all */
    snprintf(path, sizeof(path), "%s/escape/secret.txt", share);
    check_bool(path_parent_resolves_within(share, path), false,
            "path_parent_resolves_within(share, below an escaping symlink)");

    /* a dangling symlink pointing out of the share: creating its target
     * would follow it out, so it must not be accepted as "not there
     * yet" either */
    snprintf(target, sizeof(target), "%s/notyet.txt", outside);
    snprintf(path, sizeof(path), "%s/dangling", share);
    symlink(target, path);
    check_bool(path_resolves_within(share, path), false,
            "path_resolves_within(share, a dangling symlink pointing out of it)");

    /* a symlink staying inside the share is followed as usual */
    snprintf(path, sizeof(path), "%s/inside-link", share);
    symlink("inside.txt", path);
    check_bool(path_resolves_within(share, path), true,
            "path_resolves_within(share, a symlink staying inside it)");
}


static void
test_datablock_bounds(void)
{
    const size_t dest_len = 64;
    const size_t payload_len = 4096;

    /* what Rhizofs_read() does: hand DataBlock_get_data_noalloc() the
     * buffer FUSE asked it to fill together with its real size, and
     * trust the callee to bound the copy */
    uint8_t * dest = calloc(dest_len, sizeof(uint8_t));
    uint8_t * payload = malloc(payload_len);
    memset(payload, 'X', payload_len);

    Rhizofs__DataBlock * dblk = DataBlock_create();
    dblk->compression = RHIZOFS__COMPRESSION_TYPE__COMPR_NONE;
    /* a malicious server declares a tiny uncompressed size - which is
     * what the bounds check used to look at - while attaching a data
     * blob far larger than the destination buffer, which is what is
     * actually copied */
    dblk->size = 1;
    dblk->data.len = payload_len;
    dblk->data.data = payload;

    if (DataBlock_get_data_noalloc(dblk, dest, dest_len) != -1) {
        fprintf(stderr, "FAIL DataBlock_get_data_noalloc() accepted a "
                "datablock holding %d bytes of data for a %d byte buffer\n",
                (int)payload_len, (int)dest_len);
        ++failures;
    }

    /* a well formed datablock still has to be copied out */
    dblk->size = 4;
    dblk->data.len = 4;
    memcpy(dblk->data.data, "abcd", 4);

    if (DataBlock_get_data_noalloc(dblk, dest, dest_len) != 4) {
        fprintf(stderr, "FAIL DataBlock_get_data_noalloc() rejected a well "
                "formed datablock\n");
        ++failures;
    }
    else if (memcmp(dest, "abcd", 4) != 0) {
        fprintf(stderr, "FAIL DataBlock_get_data_noalloc() did not copy the "
                "data of a well formed datablock\n");
        ++failures;
    }

    DataBlock_destroy(dblk);
    free(dest);
}


int
main(void)
{
    test_path_join();
    test_path_containment();
    test_datablock_bounds();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    printf("all checks passed\n");
    return 0;
}
