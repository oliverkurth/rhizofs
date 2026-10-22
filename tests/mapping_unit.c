/* Verify the wire extension preserves link counts and accepts old peers. */
#include "mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "failed: %s at line %d\n", #condition, __LINE__); \
        exit(1); \
    } \
} while (0)

static void check_count(nlink_t count, bool legacy)
{
    struct stat original = {0}, result = {0};
    original.st_mode = S_IFREG | 0600;
    original.st_nlink = count;
    Rhizofs__Attrs *attrs = Attrs_create(&original, NULL);
    CHECK(attrs);
    CHECK(attrs->has_nlink);
    CHECK(attrs->nlink == count);
    if (legacy) {
        /* This produces the same message as an older server: no field 8. */
        attrs->has_nlink = 0;
    }
    size_t size = rhizofs__attrs__get_packed_size(attrs);
    uint8_t *buffer = malloc(size);
    CHECK(buffer);
    CHECK(rhizofs__attrs__pack(attrs, buffer) == size);
    Rhizofs__Attrs *received = rhizofs__attrs__unpack(NULL, size, buffer);
    CHECK(received);
    CHECK(Attrs_copy_to_stat(received, &result));
    CHECK(result.st_nlink == (legacy ? 1 : count));
    rhizofs__attrs__free_unpacked(received, NULL);
    Attrs_destroy(attrs);
    free(buffer);
}

int main(void)
{
    check_count(0, false);
    check_count(1, false);
    check_count(2, false);
    check_count(70000, false);
    check_count(2, true);
    return 0;
}
