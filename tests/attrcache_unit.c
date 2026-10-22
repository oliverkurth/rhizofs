/* Concurrent lookups, replacements, removals and eviction share one cache.
 * Also run this tool under ASan/TSan to detect invalid node lifetimes. */
#include "fs/attrcache.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "failed: %s at line %d\n", #condition, __LINE__); \
        abort(); \
    } \
} while (0)

static AttrCache cache;

static void *worker(void *arg)
{
    (void)arg;
    for (unsigned int i = 0; i < 20000; i++) {
        char path[32];
        /* A hot key exercises replacement/removal of the same node. The
         * other keys exceed the capacity and exercise eviction races. */
        snprintf(path, sizeof(path), "/file-%u", i % 2 ? 0 : i % 128);
        if (i % 3 == 0) {
            AttrCache_remove(&cache, path);
        } else {
            CacheEntry *entry = CacheEntry_create();
            CHECK(entry);
            entry->cache_creation_ts = time(NULL);
            entry->stat_result.st_size = 123;
            char *key = strdup(path);
            CHECK(key);
            CHECK(AttrCache_set(&cache, key, entry));
        }
        struct stat result;
        if (AttrCache_copy_stat(&cache, path, &result)) {
            CHECK(result.st_size == 123);
        }
        if (i % 101 == 0) {
            CHECK(AttrCache_shrink(&cache));
        }
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[4];
    CHECK(AttrCache_init(&cache, 64, 60));
    for (unsigned int i = 0; i < 4; i++) {
        CHECK(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    }
    for (unsigned int i = 0; i < 4; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(hash_count(cache.hashtable) <= 64);
    AttrCache_deinit(&cache);
    return 0;
}
