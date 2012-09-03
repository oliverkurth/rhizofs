#include "attrcache.h"

#include <string.h>
#include <time.h>

#include "../hashfunc.h"
#include "../dbg.h"

// prototypes
static hnode_t * CacheEntry_hash_create(void * context);
static void CacheEntry_hash_destroy(hnode_t * node, void * context);
bool AttrCache_entry_is_deprecated(const AttrCache * attrcache, const CacheEntry * cache_entry);
void Attrcache_lock_modify_mutex(AttrCache * attrcache);
void Attrcache_unlock_modify_mutex(AttrCache * attrcache);

// #### CacheEntry ############################################

inline CacheEntry *
CacheEntry_create()
{
    CacheEntry * cache_entry = NULL;
    cache_entry = calloc(sizeof(CacheEntry), 1);
    check_mem(cache_entry);

    cache_entry->cache_creation_ts = 0;; // default - way back in the past

    return cache_entry;
error:
    return NULL;
}


inline void
CacheEntry_destroy(CacheEntry * cache_entry)
{
    free(cache_entry);
}

static hnode_t *
CacheEntry_hash_create(void * context)
{
    (void) context;
    return (hnode_t *)calloc(sizeof(hnode_t), 1);
}


static void
CacheEntry_hash_destroy(hnode_t * node, void * context)
{
    (void) context;

    CacheEntry_destroy(hnode_get(node));
    free((char *)hnode_getkey(node));
    free(node);
}


// #### AttrCache ###########################################

bool
AttrCache_init(AttrCache * attrcache, size_t max_size, unsigned int max_age_sec)
{
    check((attrcache != NULL), "the attrcache parameter is NULL");

    attrcache->hashtable = hash_create(max_size,
            (hash_comp_t)strcmp,
            (hash_fun_t)Hashfunc_djb2);
    check_mem(attrcache->hashtable);

    hash_set_allocator(attrcache->hashtable,
            CacheEntry_hash_create,
            CacheEntry_hash_destroy,
            NULL);

    attrcache->max_age_sec = max_age_sec;
    attrcache->batch_size = ATTRCACHE_DEFAULT_BATCH_SIZE;

    check(pthread_mutex_init(&(attrcache->mutex_modify), NULL) == 0,
            "Could not initialize modify mutex")

    return true;

error:
    AttrCache_deinit(attrcache);
    return false;
}


void
AttrCache_deinit(AttrCache * attrcache)
{
    if (attrcache) {
        if (attrcache->hashtable) {
            hash_free_nodes(attrcache->hashtable);
            hash_destroy(attrcache->hashtable);
        }
        if (pthread_mutex_destroy(&(attrcache->mutex_modify)) != 0) {
            log_err("Could not destroy modify mutex");
        }
    }
    attrcache = NULL;
}


CacheEntry *
AttrCache_get(AttrCache * attrcache, const char * path)
{
    check(attrcache != NULL, "passed attrcache is null");
    check(path != NULL, "given path is null");

    debug("attrcache_get %s", path);

	CacheEntry * cache_entry = NULL;
    hnode_t * hash_node = hash_lookup(attrcache->hashtable, path);
    if (hash_node) {

        cache_entry = hnode_get(hash_node);

        // check timestamp
        if (AttrCache_entry_is_deprecated(attrcache, cache_entry)) {
            hash_delete_free(attrcache->hashtable, hash_node);
            cache_entry = NULL;
        }
    }

    if (cache_entry) {
        debug("HIT: Found CacheEntry for %s in cache", path);
    }
    else {
        debug("MISS: No CacheEntry for %s in cache", path);
    }

    return cache_entry;
error:
    return NULL;
}


inline bool
AttrCache_copy_stat(AttrCache * attrcache, const char * path, struct stat * stat_result)
{
    bool found = false;
    check(stat_result != NULL, "passed stat_result is null");

    Attrcache_lock_modify_mutex(attrcache);

    CacheEntry * cache_entry = AttrCache_get(attrcache, path);
    if (cache_entry) {
        memcpy(stat_result, &(cache_entry->stat_result), sizeof(struct stat));
        found = true;
    }
    Attrcache_unlock_modify_mutex(attrcache);

    return found;
error:
    Attrcache_unlock_modify_mutex(attrcache);
    return false;
}


bool
AttrCache_set(AttrCache * attrcache, char * path, CacheEntry * cache_entry)
{
    check(attrcache != NULL, "passed attrcache is null");
    check(cache_entry != NULL, "passed cache_entry is null");

    debug("attrcache_set %s", path);

    if (attrcache->hashtable->hash_maxcount == 0) {
        debug("Attrcache hastable allows 0 entries - nothing will be added");
        return true;
    }

    // make room for new keys if the cache is full
    if (hash_isfull(attrcache->hashtable)) {
        AttrCache_shrink(attrcache);
    }


    Attrcache_lock_modify_mutex(attrcache);

    // remove the old entry if there is one
    hnode_t * hash_node = hash_lookup(attrcache->hashtable, path);
    if (hash_node) {
        debug("Replacing %s in cache", path);
        hash_delete_free(attrcache->hashtable, hash_node);
    }

    check(hash_alloc_insert(attrcache->hashtable, path, cache_entry) == 1,
            "could not add cacheEntry to hash");
    Attrcache_unlock_modify_mutex(attrcache);

    return true;
error:
    Attrcache_unlock_modify_mutex(attrcache);
    return false;
}


void
AttrCache_remove(AttrCache * attrcache, const char * path)
{
    if (!attrcache || !path) {
        return;
    }

    hnode_t * hash_node = hash_lookup(attrcache->hashtable, path);
    if (hash_node) {
        debug("Removing %s from cache", path);

        Attrcache_lock_modify_mutex(attrcache);
        hash_delete_free(attrcache->hashtable, hash_node);
        Attrcache_unlock_modify_mutex(attrcache);
    }
}


bool
AttrCache_shrink(AttrCache * attrcache)
{
    check(attrcache != NULL, "passed attrcache is null");


    // get real number of entries to remove
    unsigned int shrink_num = attrcache->batch_size < hash_count(attrcache->hashtable) ?
                attrcache->batch_size : hash_count(attrcache->hashtable);

    debug("attrcache_shrink %d", shrink_num);

    if (shrink_num == 0) {
        return true;
    }

    time_t current_time = time(NULL);
    check((current_time != -1), "could not fetch current time");

    size_t nodes_removed_count = 0;

    Attrcache_lock_modify_mutex(attrcache);

    hscan_t hash_scan;
    hnode_t * hash_node = NULL;
	hash_scan_begin(&hash_scan, attrcache->hashtable);
	while ((hash_node = hash_scan_next(&hash_scan))) {
        CacheEntry * cache_entry = hnode_get(hash_node);

        if ((time_t)(cache_entry->cache_creation_ts + attrcache->max_age_sec) < current_time) {
            // entry is already above max age - immdediately delete it
            hash_scan_delete(attrcache->hashtable, hash_node);
            nodes_removed_count++;
        }
    }

    // check if deleting the depracated entries made enough space,
    // otherwise simply delete the first entries until shrink_num is
    // reached
    if (nodes_removed_count < shrink_num) {
        hash_scan_begin(&hash_scan, attrcache->hashtable);
        while( (nodes_removed_count < shrink_num) && (hash_node = hash_scan_next(&hash_scan)) ) {
            hash_scan_delete(attrcache->hashtable, hash_node);
            nodes_removed_count++;
        }
    }
    debug("Removed %d nodes from attrcache", nodes_removed_count);

    Attrcache_unlock_modify_mutex(attrcache);
    return true;

error:
    Attrcache_unlock_modify_mutex(attrcache);
    return false;
}


/**
 * check if a entry is beyond it max_age
 *
 * returns true if the entry is to old
 */
inline bool
AttrCache_entry_is_deprecated(const AttrCache * attrcache, const CacheEntry * cache_entry)
{
    time_t current_time = time(NULL);
    check((current_time != -1), "could not fetch current time");

    bool is_deprecated = (bool)((time_t)(cache_entry->cache_creation_ts + attrcache->max_age_sec) < current_time);

    if (is_deprecated) {
        debug("CacheEntry is deprecated - older than %d seconds", (int)attrcache->max_age_sec);
    }

    return is_deprecated;
error:
    return true;
}

/**
 *
 */
inline void
Attrcache_lock_modify_mutex(AttrCache * attrcache)
{
    if (attrcache) {
        debug("Locking attrcache modify mutex");
        check(pthread_mutex_lock(&(attrcache->mutex_modify)) == 0,
                "locking modify mutex failed");
    }
    return;
error:
    return;
}


/**
 *
 */
inline void
Attrcache_unlock_modify_mutex(AttrCache * attrcache)
{
    if (attrcache) {
        debug("Unlocking attrcache modify mutex");
        check(pthread_mutex_unlock(&(attrcache->mutex_modify)) == 0,
                "unlocking modify mutex failed");
    }
    return;
error:
    return;
}
