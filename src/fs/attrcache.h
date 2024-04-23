#ifndef __fs_attrache_h__
#define __fs_attrache_h__

#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <pthread.h>

#include "../kazlib/hash.h"


#define ATTRCACHE_DEFAULT_BATCH_SIZE 50


typedef struct CacheEntry {
    struct stat stat_result;

    // timestamp of the creation of this cache entry
    time_t cache_creation_ts;
} CacheEntry;


typedef struct AttrCache {
    hash_t * hashtable;

    // max age of a entry in the cache before it gets deleted
    // in seconds
    unsigned int max_age_sec;

    // number of entries getting modified on
    // batch operations like shrink.
    // default is ATTRCACHE_DEFAULT_BATCH_SIZE
    unsigned int batch_size;

    pthread_mutex_t mutex_modify;

} AttrCache;


CacheEntry * CacheEntry_create();
void CacheEntry_destroy(CacheEntry * cache_entry);

/**
 * initialize the attrcache
 *
 * returns false on error
 */
bool AttrCache_init(AttrCache * attrcache, size_t max_size, unsigned int max_age_sec);

/**
 */
void AttrCache_deinit(AttrCache * attrcache);


/**
 * return a pointer to the CacheEntry structure of the entry.
 * the allocated memory of the pointer still belongs to the cache
 *
 * does not perform any locking
 *
 * returns NULL if the entry is not found.
 */
CacheEntry * AttrCache_get(AttrCache * attrcache, const char * path);

/**
 * get the stat_result from the cache and copy it to the
 * passed pointer
 *
 * will lock the cache for modifications during this operation
 *
 * returns false if the cacheEntry is not found, true if the copy
 * was successful
 */
bool AttrCache_copy_stat(AttrCache * attrcache, const char * path, struct stat * stat_result);

/**
 * add a cache entry
 *
 * will take ownership of the cache_entry and path.
 * returns true on success
 */
bool AttrCache_set(AttrCache * attrcache, char * path, CacheEntry * cache_entry);


/**
 * remove entry from the cache
 */
void AttrCache_remove(AttrCache * attrcache, const char * path);


/**
 * shrink the number of entries by "batch_size" entries
 *
 * oldest entries get removed first
 * returns true on success
 */
bool AttrCache_shrink(AttrCache * attrcache);

#endif // __fs_attrache_h__
