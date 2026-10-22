/* Inject resolver allocation failures and legacy chmod compatibility errors. */
#include "path.h"
#include "response.h"
#include "server/servedir.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int fail_strdup_call;
static int strdup_calls;
static bool fail_dup;

static char *test_strdup(const char *value)
{
    if (++strdup_calls == fail_strdup_call) {
        errno = ENOMEM;
        return NULL;
    }
    return strdup(value);
}

static int test_dup(int fd)
{
    if (fail_dup) {
        errno = ENOMEM;
        return -1;
    }
    return dup(fd);
}

#if defined(__linux__) && defined(O_PATH)
static int nofollow_errno;
static int proc_errno;
static int proc_calls;
static int legacy_calls;
static int swap_parentfd = -1;
static const char *swap_target;

static int test_fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    if (flags == AT_SYMLINK_NOFOLLOW && nofollow_errno != 0) {
        errno = nofollow_errno;
        return -1;
    }
    if (flags == 0) {
        ++legacy_calls;
    }
    return fchmodat(dirfd, path, mode, flags);
}

static int test_chmod(const char *path, mode_t mode)
{
    ++proc_calls;
    /* Swap the checked leaf just before chmod follows the fd magic link. */
    if (swap_parentfd != -1) {
        if (renameat(swap_parentfd, "target", swap_parentfd, "held") != 0 ||
                symlinkat(swap_target, swap_parentfd, "target") != 0) abort();
    }
    if (proc_errno != 0) {
        errno = proc_errno;
        return -1;
    }
    return chmod(path, mode);
}

#define fchmodat test_fchmodat
#define chmod test_chmod
#endif

/* Keep the resolver private in production; intercept only its dependencies
 * in this test translation unit. Link without server/servedir.o. */
#define strdup test_strdup
#define dup test_dup
#include "../src/server/servedir.c"
#undef strdup
#undef dup
#if defined(__linux__) && defined(O_PATH)
#undef fchmodat
#undef chmod
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "failed: %s at line %d\n", #condition, __LINE__); \
        exit(1); \
    } \
} while (0)

static void check_failure(ServeDir *share, const char *path, int expected_errno)
{
    Rhizofs__Response response = RHIZOFS__RESPONSE__INIT;
    char *leaf = NULL;
    int parentfd = -1;
    strdup_calls = 0;
    CHECK(ServeDir_parent(share, &response, path, &parentfd, &leaf) == -1);
    CHECK(Response_get_errno(&response) == expected_errno);
    CHECK(leaf == NULL);
    CHECK(parentfd == -1);
}

#if defined(__linux__) && defined(O_PATH)
static void test_chmod_compatibility(void)
{
    char work[] = "/tmp/rhizo-chmod-unit-XXXXXX";
    CHECK(mkdtemp(work));
    int parentfd = open(work, O_PATH | O_DIRECTORY | O_CLOEXEC);
    CHECK(parentfd != -1);
    int fd = openat(parentfd, "target", O_CREAT | O_WRONLY, 0600);
    CHECK(fd != -1);
    close(fd);
    CHECK(mkdirat(parentfd, "directory", 0700) == 0);
    fd = openat(parentfd, "outside", O_CREAT | O_WRONLY, 0600);
    CHECK(fd != -1);
    close(fd);
    CHECK(symlinkat("outside", parentfd, "link") == 0);

    /* Inject old glibc's result, and the missing-/proc result returned by
     * later glibc on older kernels. Exercise unreadable files/directories
     * using real filesystem operations, without needing another libc. */
    const int unsupported[] = {ENOTSUP, EOPNOTSUPP};
    const int proc_failures[] = {0, ENOENT, ENOTDIR};
    const char *leaves[] = {"target", "directory"};
    struct stat sb;
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        nofollow_errno = unsupported[i];
        for (size_t j = 0; j < sizeof(proc_failures) / sizeof(proc_failures[0]); ++j) {
            if (proc_failures[j] == 0 && access("/proc/self/fd", X_OK) != 0) continue;
            proc_errno = proc_failures[j];
            for (size_t k = 0; k < sizeof(leaves) / sizeof(leaves[0]); ++k) {
                CHECK(fchmodat(parentfd, leaves[k], 0, 0) == 0);
                proc_calls = legacy_calls = 0;
                if (proc_errno == 0) {
                    CHECK(ServeDir_chmodat(parentfd, leaves[k], 0700) == 0);
                } else {
                    CHECK(ServeDir_chmodat(parentfd, leaves[k], 0700) == -1);
                    CHECK(errno == EOPNOTSUPP);
                }
                CHECK(fstatat(parentfd, leaves[k], &sb, 0) == 0);
                CHECK((sb.st_mode & 0777) == (proc_errno == 0 ? 0700 : 0));
                CHECK(proc_calls == 1);
                CHECK(legacy_calls == 0);
            }
            proc_calls = legacy_calls = 0;
            CHECK(ServeDir_chmodat(parentfd, "link", 0777) == -1);
            CHECK(errno == EOPNOTSUPP);
            CHECK(proc_calls == 0 && legacy_calls == 0);
            CHECK(fstatat(parentfd, "outside", &sb, 0) == 0);
            CHECK((sb.st_mode & 0777) == 0600);
            CHECK(ServeDir_chmodat(parentfd, "missing", 0777) == -1);
            CHECK(errno == ENOENT);
            CHECK(proc_calls == 0 && legacy_calls == 0);
        }
    }

    /* Permission failures are errors, not a reason to retry unsafely. */
    proc_calls = legacy_calls = 0;
    nofollow_errno = EACCES;
    CHECK(ServeDir_chmodat(parentfd, "target", 0600) == -1);
    CHECK(errno == EACCES);
    CHECK(proc_calls == 0 && legacy_calls == 0);
    nofollow_errno = ENOTSUP;
    proc_errno = EACCES;
    CHECK(ServeDir_chmodat(parentfd, "target", 0600) == -1);
    CHECK(errno == EACCES && legacy_calls == 0);
    proc_errno = ENOENT;
    CHECK(ServeDir_chmodat(parentfd, "target", 0600) == -1);
    CHECK(errno == EOPNOTSUPP && legacy_calls == 0);

    /* Swap the checked leaf before the /proc call. With /proc available,
     * only the held inode may change; without it, neither inode may change. */
    CHECK(unlinkat(parentfd, "target", 0) == 0);
    for (size_t j = 0; j < sizeof(proc_failures) / sizeof(proc_failures[0]); ++j) {
        if (proc_failures[j] == 0 && access("/proc/self/fd", X_OK) != 0) continue;
        fd = openat(parentfd, "target", O_CREAT | O_WRONLY, 0600);
        CHECK(fd != -1);
        close(fd);
        proc_errno = proc_failures[j];
        legacy_calls = 0;
        swap_parentfd = parentfd;
        swap_target = "outside";
        if (proc_errno == 0) {
            CHECK(ServeDir_chmodat(parentfd, "target", 0620) == 0);
        } else {
            CHECK(ServeDir_chmodat(parentfd, "target", 0620) == -1);
            CHECK(errno == EOPNOTSUPP);
        }
        swap_parentfd = -1;
        CHECK(legacy_calls == 0);
        CHECK(fstatat(parentfd, "held", &sb, 0) == 0);
        CHECK((sb.st_mode & 0777) == (proc_errno == 0 ? 0620 : 0600));
        CHECK(fstatat(parentfd, "outside", &sb, 0) == 0);
        CHECK((sb.st_mode & 0777) == 0600);
        CHECK(unlinkat(parentfd, "held", 0) == 0);
        CHECK(unlinkat(parentfd, "target", 0) == 0);
    }
    nofollow_errno = proc_errno = 0;
    CHECK(unlinkat(parentfd, "directory", AT_REMOVEDIR) == 0);
    CHECK(unlinkat(parentfd, "link", 0) == 0);
    CHECK(unlinkat(parentfd, "outside", 0) == 0);
    close(parentfd);
    CHECK(rmdir(work) == 0);
}
#endif

int main(void)
{
#if defined(__linux__) && defined(O_PATH)
    test_chmod_compatibility();
#endif
    char directory[] = "/tmp/rhizo-server-unit-XXXXXX";
    CHECK(mkdtemp(directory));
    ServeDir share = {.directory = directory,
        .directory_fd = open(directory, O_RDONLY | O_DIRECTORY)};
    CHECK(share.directory_fd != -1);
    check_failure(&share, NULL, EINVAL);
    check_failure(&share, "/../outside", EACCES);
    fail_dup = true;
    check_failure(&share, "/target", ENOMEM);
    fail_dup = false;

    /* Exercise both allocations for the root and for a named leaf. */
    const char *paths[] = {"/", "/target"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        for (fail_strdup_call = 1; fail_strdup_call <= 2; ++fail_strdup_call) {
            check_failure(&share, paths[i], ENOMEM);
        }
        fail_strdup_call = 0;
        Rhizofs__Response response = RHIZOFS__RESPONSE__INIT;
        char *leaf = NULL;
        int parentfd = -1;
        CHECK(ServeDir_parent(&share, &response, paths[i], &parentfd, &leaf) == 0);
        CHECK(Response_get_errno(&response) == 0);
        CHECK(leaf);
        CHECK(parentfd != -1);
        free(leaf);
        close(parentfd);
    }
    close(share.directory_fd);
    CHECK(rmdir(directory) == 0);
    return 0;
}
