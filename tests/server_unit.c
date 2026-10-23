/* Inject resolver allocation failures without exhausting process memory. */
#include "path.h"
#include "response.h"
#include "server/servedir.h"

#include <unistd.h>

static bool fail_join;
static bool fail_containment;

static int test_path_join(const char *base, const char *path, char **joined)
{
    if (fail_join) {
        errno = ENOMEM;
        return -1;
    }
    return path_join(base, path, joined);
}

static bool test_containment(const char *base, const char *path)
{
    if (fail_containment) {
        errno = ENOMEM;
        return false;
    }
    return path_resolves_within(base, path);
}

static bool test_parent_containment(const char *base, const char *path)
{
    if (fail_containment) {
        errno = ENOMEM;
        return false;
    }
    return path_parent_resolves_within(base, path);
}

/* Keep the resolver private in production; intercept only its dependencies
 * in this test translation unit. Link without server/servedir.o. */
#define path_join test_path_join
#define path_resolves_within test_containment
#define path_parent_resolves_within test_parent_containment
#include "../src/server/servedir.c"
#undef path_join
#undef path_resolves_within
#undef path_parent_resolves_within

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "failed: %s at line %d\n", #condition, __LINE__); \
        exit(1); \
    } \
} while (0)

static void check_failure(ServeDir *share, const char *path,
                          PathLeafMode mode, int expected_errno)
{
    Rhizofs__Response response = RHIZOFS__RESPONSE__INIT;
    char *fullpath = NULL;
    CHECK(ServeDir_resolve(share, &response, path, &fullpath, mode) == -1);
    CHECK(Response_get_errno(&response) == expected_errno);
    CHECK(fullpath == NULL);
}

int main(void)
{
    char directory[] = "/tmp/rhizo-server-unit-XXXXXX";
    CHECK(mkdtemp(directory));
    ServeDir share = {.directory = directory};
    for (int mode = PATH_FOLLOW_LEAF; mode <= PATH_KEEP_LEAF; mode++) {
        check_failure(&share, NULL, mode, EINVAL);
        check_failure(&share, "/../outside", mode, EACCES);
        fail_join = true;
        check_failure(&share, "/target", mode, ENOMEM);
        fail_join = false;
        fail_containment = true;
        check_failure(&share, "/target", mode, ENOMEM);
        fail_containment = false;

        Rhizofs__Response response = RHIZOFS__RESPONSE__INIT;
        char *fullpath = NULL;
        CHECK(ServeDir_resolve(&share, &response, "/target", &fullpath, mode) == 0);
        CHECK(Response_get_errno(&response) == 0);
        CHECK(fullpath);
        free(fullpath);
    }
    CHECK(rmdir(directory) == 0);
    return 0;
}
