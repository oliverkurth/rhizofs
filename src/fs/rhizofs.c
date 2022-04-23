#include "rhizofs.h"

#include <stdbool.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>

#include "../mapping.h"
#include "../request.h"
#include "../response.h"
#include "../datablock.h"
#include "socketpool.h"
#include "../version.h"
#include "../dbg.h"
#include "../path.h"
#include "../helptext.h"
#include "attrcache.h"

// use the 2.6 fuse api
#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif
#include <fuse.h>

#include <zmq.h>


/** private data to be stored in the fuse context */
typedef struct RhizoPriv {
    /** the zeromq context */
    void * context;
} RhizoPriv;

typedef struct RhizoSettings {
    /** the name of the zmq socket to connect to */
    char * host_socket;

    /** timeout (in seconds) after which the filesystem will stop waiting
     *  * for a response from the server
     *  * for being able to send the request to the server
     *
     *  instead return a errno = EAGAIN
     */
    uint32_t timeout;

    /** check socket connection.
     * this is set to false if the program is only supposed to
     * print its help text and exit */
    bool check_socket_connection;

} RhizoSettings;


/** enumerations for commandline options */
enum {
    KEY_HELP,
    KEY_VERSION,
};


static struct fuse_opt rhizo_opts[] = {
    FUSE_OPT_KEY("-V",             KEY_VERSION),
    FUSE_OPT_KEY("--version",      KEY_VERSION),
    FUSE_OPT_KEY("-h",             KEY_HELP),
    FUSE_OPT_KEY("--help",         KEY_HELP),
    FUSE_OPT_END
};

// Prototypes
bool Rhizofs_convert_attrs_stat(Rhizofs__Attrs * attrs, struct stat * stbuf);
int Rhizofs_getattr_remote(const char *path, struct stat *stbuf);
static inline RhizoPriv * RhizoPriv_create();
static inline void RhizoPriv_destroy(RhizoPriv * priv);


/** global settings store */
static RhizoSettings settings;

static SocketPool socketpool;
static AttrCache attrcache;


/**
 * filesystem initialization
 *
 * provides:
 * - starting of background threads
 */
static void *
Rhizofs_init(struct fuse_conn_info * UNUSED_PARAMETER(conn))
{
    RhizoPriv * priv = NULL;

    priv = RhizoPriv_create();
    check(priv, "Could not create RhizoPriv context");

    /* create the socket pool */
    check((SocketPool_init(&socketpool, priv->context, settings.host_socket, ZMQ_REQ) == true),
            "Could not initialize the socket pool");

    check((AttrCache_init(&attrcache, ATTRCACHE_MAXSIZE, ATTRCACHE_DEFAULT_MAXAGE_SEC) == true),
            "could not initialize the attrcache");

    return priv;

error:

    SocketPool_deinit(&socketpool);
    AttrCache_deinit(&attrcache);
    RhizoPriv_destroy(priv);

    /* exiting here is the last fallback when
       setting up the socket fails. see the NOTES
       file
    */
    fuse_exit(fuse_get_context()->fuse);
    return NULL;
}

/**
 * destroy/free the resources allocated by the filesystem
 */
static void
Rhizofs_destroy(void * UNUSED_PARAMETER(data))
{
    SocketPool_deinit(&socketpool);
    AttrCache_deinit(&attrcache);

    struct fuse_context * fcontext = fuse_get_context();
    if (fcontext) {
        RhizoPriv * priv = (RhizoPriv *)fcontext->private_data;
        RhizoPriv_destroy(priv);
    }
}


/**
 * send the request and wait for a reponse
 *
 * "socket_to_use" is an optional parameter. if it is not null, the function
 * will use this socket. When it is NULL, a socket from the socketpool will
 * be used.
 *
 * "check_fuse_interrupts" enables checking for any interupts/signals
 * detected by libfuse. set to false to use this function outside
 * of a valid fuse_context
 *
 * returns NULL on error, otherwise a Response the caller
 * is responsible tor free.
 */
Rhizofs__Response *
Rhizofs_communicate(Rhizofs__Request * req, int * err, void * socket_to_use, bool check_fuse_interrupts)
{
    void * sock = NULL;
    int rc;
    Rhizofs__Response * response = NULL;
    zmq_msg_t msg_req;
    zmq_msg_t msg_resp;
    struct fuse_context * fcontext = fuse_get_context();
    bool renew_socket = false;

    (*err) = 0;

    if (socket_to_use) {
        sock = socket_to_use;
    }
    else {
        /* get a socket from the socketpool, return an errno on failure */
        sock = SocketPool_get_socket(&socketpool);
    }

    if (sock == NULL) {
        (*err) = ENOTSOCK; /* Socket operation on non-socket */
        log_and_error("Could not fetch socket from socketpool");
    };

    if (Request_pack(req, &msg_req) != true) {
        (*err) = errno;
        log_and_error("Could not pack request");
    }

    renew_socket = true;
    uint32_t repetition = 0;
    do {
        rc = zmq_msg_send(&msg_req, sock, 0);
        if (rc == -1) {
            if ((errno == EAGAIN) || (errno == EFSM)) {
                /* sleep for a short time before retrying
                 * also sleep on EFSM as the server might just be starting up
                 * with the socket not being in the correct state.
                 */
                usleep(SEND_SLEEP_USEC);

                if (check_fuse_interrupts) {
                    if ((fuse_interrupted() != 0) || fuse_exited(fcontext->fuse)) {
                        (*err) = EINTR;
                        log_info("The request has been interrupted");
                        goto error;
                    }
                }
            }
            else {
                (*err) = EIO;
                renew_socket = false;
                log_and_error("Could not send request [errno: %d]", errno);
            }
        }

        ++repetition;

        // check for a timeout while waiting for being able to send the request
        uint32_t seconds_waited = (repetition * SEND_SLEEP_USEC) / (1000 * 1000);
        if (seconds_waited >= settings.timeout) {
            log_info("Timeout after trying to send request to server for %d seconds.", seconds_waited);
            (*err) = EAGAIN;
            goto error;
        }
    } while (rc == -1);

    zmq_pollitem_t pollset[] = {
        { sock, 0, ZMQ_POLLIN, 0 }
    };

    if (zmq_msg_init(&msg_resp) != 0) {
        (*err) = ENOMEM;
        log_and_error("Could not initialize response message");
    }

    rc = 1; /* set to a non-zero value to prevent exit of loop
             * before a response arrived */
    repetition = 0;
    do {
        zmq_poll(pollset, 1, POLL_TIMEOUT_MSEC);

        if (pollset[0].revents & ZMQ_POLLIN) {
            rc = zmq_msg_recv(&msg_resp, sock, 0);
            if (rc != -1) {  /* successfuly received response */
                response = Response_from_message(&msg_resp);
                if (response == NULL) {
                    (*err) = EIO;
                    log_and_error("Could not unpack response");
                } else
                    break;
            }

            else {
                (*err) = EIO;
                log_and_error("Failed to receive response from server");
            }
        }
        else {
            /* no response available at this time
             * check if fuse has received an interrupt
             * while waiting for a response
             */
            if (check_fuse_interrupts) {
                if ((fuse_interrupted() != 0) || fuse_exited(fcontext->fuse)) {
                    log_info("The request has been interrupted");
                    (*err) = EINTR;
                    goto error;
                }
            }
        }

        ++repetition;

        // check for response timeout
        uint32_t seconds_waited = (repetition * POLL_TIMEOUT_MSEC) / 1000;
        if (seconds_waited >= settings.timeout) {
            log_info("Timeout after waiting for response from server for %d seconds.", seconds_waited);
            (*err) = EAGAIN;
            goto error;
        }
    } while (rc == -1);

    /* close request after receiving reply as it is sure 0mq does not
     * hold a reference anymore */
    zmq_msg_close(&msg_req);
    zmq_msg_close(&msg_resp);

    if (response != NULL)
        *err = Response_get_errno(response);
    else
        *err = EIO;

    return response;

error:
    if (renew_socket && !socket_to_use) {
        // renew socket as soon there is any chance of it being in an
        // inconsistent state
        //
        // this basically implements the "The Lazy Pirate Pattern" described
        // in the ZMQ Guide
        debug("Renewing socket");
        SocketPool_renew_socket(&socketpool);
    }
    zmq_msg_close(&msg_req);
    zmq_msg_close(&msg_resp);
    return NULL;
}



/**
 * convert a Rhizofs_Attrs struct to a stat
 * the stat has to be allocated
 * by the caller
 */
inline bool
Rhizofs_convert_attrs_stat(Rhizofs__Attrs * attrs, struct stat * stbuf)
{
    struct fuse_context * fcontext = fuse_get_context();

    check((Attrs_copy_to_stat(attrs, stbuf) == true),
            "could not copy Attrs to stat");

    debug("mode: %o",stbuf->st_mode );

    /* set the uid ad gid of the calling process
     * do not set anything if the server-user is
     * not the user / in the group. this will cause
     * the FS to return "root"
     */
    if (attrs->is_owner != 0) {
        stbuf->st_uid = fcontext->uid;
    }
    if (attrs->is_in_group != 0) {
        /* this might be a bit to ambiguous .. think of something better */
        stbuf->st_gid = fcontext->gid;
    }

    return true;

error:
    return false;
}


/*******************************************************************/
/* filesystem methods                                              */
/*******************************************************************/

#define OP_STD_RETURNED_ERR EIO

#define  OP_INIT(REQ, RESP, RET_ERR)   \
    int RET_ERR = OP_STD_RETURNED_ERR; \
    Rhizofs__Request REQ; \
    Rhizofs__Response * RESP = NULL; \
    if (!Request_init(&REQ)) { \
        RET_ERR = ENOMEM; \
        log_and_error("Could not initialize Request"); \
    }

#define OP_COMMUNICATE_USING_SOCKET(REQ, RESP, RET_ERR, SOCK, CHECK_FUSE_INTERRUPTS) \
    RESP = Rhizofs_communicate(&REQ, &RET_ERR, SOCK, CHECK_FUSE_INTERRUPTS); \
    check_debug((RET_ERR == 0), "Server reported an error: %d", RET_ERR); \
    check((RESP != NULL), "communicate failed");

#define OP_COMMUNICATE(REQ, RESP, RET_ERR) \
    OP_COMMUNICATE_USING_SOCKET(REQ, RESP, RET_ERR, NULL, true)

#define OP_DEINIT(REQ, RESP) \
    Request_deinit(&REQ); \
    Response_from_message_destroy(RESP);


static int
Rhizofs_readdir(const char * path, void * buf,
    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info * fi)
{
    unsigned int entry_n = 0;
    char * path_entry = NULL;
    CacheEntry * cache_entry = NULL;

    (void) offset;
    (void) fi;

    OP_INIT(request, response, returned_err);

    request.path = (char *)path;
    request.requesttype = RHIZOFS__REQUEST_TYPE__READDIR;

    OP_COMMUNICATE(request, response, returned_err)

    time_t current_time = time(NULL);
    check((current_time != -1), "could not fetch current time");

    for (entry_n=0; entry_n<response->n_directory_entries; ++entry_n) {
        check((response->directory_entries[entry_n]->name != NULL),
            "attrs is missing the name");

        // add to file list
        if (filler(buf, response->directory_entries[entry_n]->name, NULL, 0)) {
            break;
        }

        // add to cache
        check(path_join(path, response->directory_entries[entry_n]->name, &path_entry) == 0,
                "could not join path for directory entry %s", response->directory_entries[entry_n]->name);
        check_mem(path_entry);
        cache_entry = CacheEntry_create();
        check_mem(cache_entry);

        cache_entry->cache_creation_ts = current_time;

        check(Rhizofs_convert_attrs_stat(response->directory_entries[entry_n], &(cache_entry->stat_result)) == true,
                "could not convert attrs");

        check(AttrCache_set(&attrcache, path_entry, cache_entry),
                "Could not add stat to AttrCache");
    }

    OP_DEINIT(request, response)
    return 0;

error:
    free(path_entry);
    CacheEntry_destroy(cache_entry);

    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_getattr(const char *path, struct stat *stbuf)
{
    if (AttrCache_copy_stat(&attrcache, path, stbuf) == false) {
        return Rhizofs_getattr_remote(path, stbuf);
    }
    return 0;
}


inline int
Rhizofs_getattr_remote(const char *path, struct stat *stbuf)
{
    CacheEntry * cache_entry = NULL;
    char * path_copy = NULL;

    OP_INIT(request, response, returned_err);

    request.path = (char *)path;
    request.requesttype = RHIZOFS__REQUEST_TYPE__GETATTR;

    OP_COMMUNICATE(request, response, returned_err)
    check((response->attrs != NULL), "Response did not contain attrs");

    check((Rhizofs_convert_attrs_stat(response->attrs, stbuf) == true),
            "could not convert attrs");

    // prepare parameters for cache entry
    path_copy = strdup(path);
    check_mem(path_copy);
    cache_entry = CacheEntry_create();
    check_mem(cache_entry);

    time_t current_time = time(NULL);
    check((current_time != -1), "could not fetch current time");
    cache_entry->cache_creation_ts = current_time;

    memcpy(&(cache_entry->stat_result), stbuf, sizeof(struct stat));

    check(AttrCache_set(&attrcache, path_copy, cache_entry),
            "Could not add stat to AttrCache");

    OP_DEINIT(request, response)
    return 0;

error:
    free(path_copy);
    CacheEntry_destroy(cache_entry);

    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_rmdir(const char * path)
{
    OP_INIT(request, response, returned_err);

    request.path = (char *)path;
    request.requesttype = RHIZOFS__REQUEST_TYPE__RMDIR;

    OP_COMMUNICATE(request, response, returned_err)
    AttrCache_remove(&attrcache, path);

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_mkdir(const char * path, mode_t mode)
{
    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__MKDIR;
    request.path = (char *)path;

    debug("mkdir mode: %d", (int)mode);
    request.permissions = Permissions_create((mode_t)mode);
    check((request.permissions != NULL), "Could not create access permissions struct");

    OP_COMMUNICATE(request, response, returned_err)

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_unlink(const char * path)
{
    OP_INIT(request, response, returned_err);

    request.path = (char *)path;
    request.requesttype = RHIZOFS__REQUEST_TYPE__UNLINK;

    OP_COMMUNICATE(request, response, returned_err)
    AttrCache_remove(&attrcache, path);

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_access(const char * path, int mask)
{
    OP_INIT(request, response, returned_err);

    request.path = (char *)path;
    request.requesttype = RHIZOFS__REQUEST_TYPE__ACCESS;

    request.permissions = Permissions_create((mode_t)mask);
    check((request.permissions != NULL), "Could not create access permissions struct");

    OP_COMMUNICATE(request, response, returned_err)

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_open(const char * path, struct fuse_file_info *fi)
{
    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__OPEN;
    request.path = (char *)path;

    request.openflags = OpenFlags_from_bitmask(fi->flags);
    check((request.openflags != NULL), "could not create openflags for request");

    OP_COMMUNICATE(request, response, returned_err)

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_create(const char * path, mode_t create_mode, struct fuse_file_info *fi)
{
    (void) fi;

    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__CREATE;
    request.path = (char *)path;

    request.permissions = Permissions_create(create_mode);
    check((request.permissions != NULL), "Could not create create permissions struct");

    OP_COMMUNICATE(request, response, returned_err)

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_read(const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    int size_read = 0;

    (void) fi;

    OP_INIT(request, response, returned_err);

    request.path = (char *)path;
    request.has_size = 1;
    request.size = (int)size;
    request.has_offset = 1;
    request.offset = (int)offset;
    request.requesttype = RHIZOFS__REQUEST_TYPE__READ;

    OP_COMMUNICATE(request, response, returned_err)
    check((Response_has_data(response) != -1), "Server did not send data in response");

    size_read = DataBlock_get_data_noalloc(response->datablock, (uint8_t *)buf, size);

    OP_DEINIT(request, response)
    return size_read;

error:
    OP_DEINIT(request, response)
    return -returned_err;

}


static int
Rhizofs_write(const char * path, const char * buf, size_t size, off_t offset,
		      struct fuse_file_info * fi)
{
    int size_write = 0;

    (void) fi;

    OP_INIT(request, response, returned_err);

    request.path = (char *)path;
    request.has_size = 1;
    request.size = (int)size;
    request.has_offset = 1;
    request.offset = (int)offset;
    request.requesttype = RHIZOFS__REQUEST_TYPE__WRITE;
    check((Request_set_data(&request, (const uint8_t *) buf, (size_t)size) == true),
            "could not set request data");

    OP_COMMUNICATE(request, response, returned_err)
    AttrCache_remove(&attrcache, path);
    check((response->has_size == 1),
            "response did not contain the number of bytes written");

    size_write = response->size;

    OP_DEINIT(request, response)
    return size_write;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}

static int
Rhizofs_truncate(const char * path, off_t offset)
{
    OP_INIT(request, response, returned_err);

    request.path = (char *)path;
    request.offset = (int)offset;
    request.has_offset = 1;
    request.requesttype = RHIZOFS__REQUEST_TYPE__TRUNCATE;

    OP_COMMUNICATE(request, response, returned_err)
    AttrCache_remove(&attrcache, path);

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_chmod(const char * path, mode_t access_mode)
{
    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__CHMOD;
    request.path = (char *)path;

    request.permissions = Permissions_create(access_mode);
    check((request.permissions != NULL), "Could not create chmod permissions struct");

    OP_COMMUNICATE(request, response, returned_err)
    AttrCache_remove(&attrcache, path);

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_utimens(const char * path, const struct timespec tv[2])
{
    time_t asec = 0, msec = 0;

    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__UTIMENS;
    request.path = (char *)path;

    request.timestamps = TimeSet_create();
    check((request.timestamps != NULL), "Could not create utimens timestamps struct");

    if (tv != NULL) {
        asec = tv[0].tv_sec;
        msec = tv[1].tv_sec;
    }

    if (asec == 0 || msec == 0) {
        struct timeval now;
        gettimeofday(&now, NULL);
        if (asec == 0)
            asec = now.tv_sec;
        if (msec == 0)
            msec = now.tv_sec;
    }

    request.timestamps->access       = asec;
    request.timestamps->modification = msec;

    OP_COMMUNICATE(request, response, returned_err)
    AttrCache_remove(&attrcache, path);

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_link(const char * path_from, const char * path_to)
{
    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__LINK;
    request.path = (char *)path_from;
    request.path_to = (char *)path_to;

    OP_COMMUNICATE(request, response, returned_err)

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_rename(const char * path_from, const char * path_to)
{
    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__RENAME;
    request.path = (char *)path_from;
    request.path_to = (char *)path_to;

    OP_COMMUNICATE(request, response, returned_err)
    AttrCache_remove(&attrcache, path_from);

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_readlink(const char * path, char * link_target, size_t len)
{
    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__READLINK;
    request.path = (char *)path;

    OP_COMMUNICATE(request, response, returned_err)
    check((response->link_target != NULL), "Response did not contain link_target");
    strncpy(link_target, response->link_target, len);
    link_target[len-1] = 0;

    debug("symlink points to %s", link_target);

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_symlink(const char * path_to, const char * path_from)
{
    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__SYMLINK;
    request.path = (char *)path_from;
    request.path_to = (char *)path_to;

    OP_COMMUNICATE(request, response, returned_err)

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_mknod(const char * path, mode_t mode, dev_t dev)
{
    (void) dev;

    if (!S_ISREG(mode)) {
        log_err("Using mknod to create something other than regular files is not supported.");
        return -EPERM;
    }

    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__MKNOD;
    request.path = (char *)path;

    request.filetype = FileType_from_local(mode);
    request.has_filetype = 1;

    request.permissions = Permissions_create(mode);
    check((request.permissions != NULL), "Could not create mknod permissions struct");

    OP_COMMUNICATE(request, response, returned_err)

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;

}


static int
Rhizofs_chown(const char * path, uid_t user, gid_t group)
{
    (void) path;
    (void) user;
    (void) group;

    log_warn("CHOWN is not (yet) supported");
    return -ENOTSUP;
}


static int
Rhizofs_statfs(const char * path, struct statvfs * svfs)
{
    OP_INIT(request, response, returned_err);

    request.requesttype = RHIZOFS__REQUEST_TYPE__STATFS;
    request.path = (char *)path;

    OP_COMMUNICATE(request, response, returned_err)
    check((response->statfs != NULL), "Response did not contain statfs");

    svfs->f_bsize = response->statfs->bsize;
    svfs->f_frsize = response->statfs->frsize;
    svfs->f_blocks = response->statfs->blocks;
    svfs->f_bfree = response->statfs->bfree;
    svfs->f_bavail = response->statfs->bavail;

    svfs->f_files = response->statfs->files;
    svfs->f_ffree = response->statfs->ffree;
    svfs->f_favail = response->statfs->favail;

    svfs->f_fsid = response->statfs->fsid;
    svfs->f_flag = response->statfs->flag;
    svfs->f_namemax = response->statfs->namemax;

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


/*
static int
Rhizofs_release(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    (void) fi;

    log_warn("RELEASE is not (yet) supported");
    return -ENOTSUP;
}


static int
Rhizofs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    (void) path;
    (void) isdatasync;
    (void) fi;

    log_warn("FSYNC is not (yet) supported");
    return -ENOTSUP;
}
*/


static struct fuse_operations rhizofs_operations = {
    .readdir    = Rhizofs_readdir,
    .init       = Rhizofs_init,
    .destroy    = Rhizofs_destroy,
    .getattr    = Rhizofs_getattr,
    .mkdir      = Rhizofs_mkdir,
    .rmdir      = Rhizofs_rmdir,
    .unlink     = Rhizofs_unlink,
    .access     = Rhizofs_access,
    .open       = Rhizofs_open,
    .read       = Rhizofs_read,
    .write      = Rhizofs_write,
    .create     = Rhizofs_create,
    .truncate   = Rhizofs_truncate,
    .chmod      = Rhizofs_chmod,
    .utimens    = Rhizofs_utimens,
    .link       = Rhizofs_link,
    .rename     = Rhizofs_rename,
    .symlink    = Rhizofs_symlink,
    .readlink   = Rhizofs_readlink,
    .mknod      = Rhizofs_mknod,
//  stubs to implement
    .chown      = Rhizofs_chown,
    .statfs     = Rhizofs_statfs
    //.release    = Rhizofs_release,
    //.fsync      = Rhizofs_fsync,
};


/** settings ******************************************************/

static inline void
Rhizofs_settings_init()
{
    memset(&settings, 0, sizeof(settings));

    // set the default timeout
    settings.timeout = TIMEOUT_DEFAULT;

    settings.check_socket_connection = true;
}


static inline void
Rhizofs_settings_deinit()
{
    free(settings.host_socket);
}


/**
 * check the settings from the command line arguments
 * returns -1 o failure, 0 on correct arguments
 */
static int
Rhizofs_settings_check()
{
    if (settings.host_socket == NULL) {
        fprintf(stderr, "Missing host");
        goto error;
    }
    return 0;

error:
    return -1;
}


/** priv **********************************************************/

/**
 * create a new pivate struct
 *
 * provides
 * - setting up a 0mq context
 *
 * returns NULL on error
 */
static inline RhizoPriv *
RhizoPriv_create()
{
    RhizoPriv * priv = NULL;

    priv = calloc(sizeof(RhizoPriv), 1);
    check_mem(priv);
    priv->context = NULL;

    priv->context = zmq_init(1);
    check((priv->context != NULL), "Could not create Zmq context");

    return priv;

error:
    return 0;
}


/**
 * destory a private struct
 */
static inline void
RhizoPriv_destroy(RhizoPriv * priv)
{
    if (priv) {
        if (priv->context != NULL) {
            zmq_term(priv->context);
            priv->context = NULL;
        }
        free(priv);
        priv = NULL;
    }
}

/*******************************************************************/
/* general methods                                                 */
/*******************************************************************/


void
Rhizofs_usage(const char * progname)
{
    fprintf(stderr,
        "usage: %s SOCKET MOUNTPOINT [options]\n"
        "\n"
        HELPTEXT_INTRO
        "\n"
        "This program implements the client-side filesystem.\n"
        "\n"
        "Parameters\n"
        "==========\n"
        "\n"
        "The parameters SOCKET and MOUNTPOINT are mandatory.\n"
        "\n"
        HELPTEXT_SOCKET
        "\n"
        "Mountpoint\n"
        "----------\n"
        "   The directory to mount the filesystem in.\n"
        "   The directory has to be empty.\n"
        "\n"
        "general options\n"
        "---------------\n"
        "    -h   --help      print help\n"
        "    -V   --version   print version\n"
        "\n"
        HELPTEXT_LOGGING
        "\n", progname
    );
}


/**
 * check if a connection to the server is possible by sending a ping
 *
 * return true when the connection was successful, otherwise false
 */
bool
Rhizofs_check_connection(RhizoPriv * priv)
{
    void * socket = NULL;
    OP_INIT(request, response, returned_err);

    check(priv, "Got an empty RhizoPriv struct");
    check(settings.host_socket, "Can not check connection. No host socket given.")

    fprintf(stdout, "Trying to connect to server at %s\n", settings.host_socket);

    socket = zmq_socket(priv->context, ZMQ_REQ);
    check((socket != NULL), "Could not create 0mq socket");

    int hwm = 1; /* prevents memory leaks when fuse interrupts while waiting on server */
    zmq_setsockopt(socket, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(socket, ZMQ_RCVHWM, &hwm, sizeof(hwm));

#ifdef ZMQ_MAKE_VERSION
#if ZMQ_VERSION >= ZMQ_MAKE_VERSION(2,1,0)
    int linger = 0;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
#endif
#endif

    if (zmq_connect(socket, settings.host_socket) != 0) {
        fprintf(stderr, "Could not connect to server at %s\n", settings.host_socket);
        log_and_error("could not connect socket to %s", settings.host_socket);
    }

    request.requesttype = RHIZOFS__REQUEST_TYPE__PING;
    OP_COMMUNICATE_USING_SOCKET(request, response, returned_err, socket, false);

    fprintf(stdout, "Connection successful. (Server version %d.%d.%d)\n", response->version->major,
            response->version->minor,  response->version->patch);

    OP_DEINIT(request, response)
    zmq_close(socket);

    return true;

error:

    if (returned_err != 0) {
        fprintf(stderr, "Connecting to server failed: %s\n", strerror(returned_err));
    }

    OP_DEINIT(request, response)
    if (socket) {
        zmq_close(socket);
    }
    return false;
}


/**
 * start the filesystem
 *
 * returns the errorcode of the process. 0 on success
 */
int
Rhizofs_fuse_main(struct fuse_args *args)
{
    RhizoPriv * priv = NULL;

    priv = RhizoPriv_create();
    check(priv, "Could not create RhizoPriv context");

    if (settings.check_socket_connection) {
        if (!Rhizofs_check_connection(priv)) {
            log_and_error("Could not connect to server");
        }
    }

    int rc = fuse_main(args->argc, args->argv, &rhizofs_operations, NULL );
    RhizoPriv_destroy(priv);

    return rc;

error:
    RhizoPriv_destroy(priv);
    return 1;
}


/**
 * parse the option string
 *
 * returning 0 means the option found and handled, returning
 * 1 means fuse should handle the option.
 */
int
Rhizofs_opt_proc(void * data, const char *arg, int key, struct fuse_args *outargs)
{
    (void) data;

    switch (key) {
        case KEY_HELP:
            settings.check_socket_connection = false;
            Rhizofs_usage(outargs->argv[0]);
            fuse_opt_add_arg(outargs, "-ho");
            Rhizofs_fuse_main(outargs);
            exit(0);

        case KEY_VERSION:
            settings.check_socket_connection = false;
            fprintf(stderr, "%s version %s\n", RHI_NAME, RHI_VERSION_FULL);
            fuse_opt_add_arg(outargs, "--version");
            Rhizofs_fuse_main(outargs);
            exit(0);

        case FUSE_OPT_KEY_NONOPT:
            if (!settings.host_socket) {
                settings.host_socket = strdup(arg); /* TODO: handle failure */
                return 0;
            }
            return 1;
    }
    return 1;
}


int
Rhizofs_run(int argc, char * argv[])
{
#define TMPBUF_SIZE 1024
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    char tmpbuf[TMPBUF_SIZE];
    int rc;

    Rhizofs_settings_init();

    fuse_opt_parse(&args, &settings, rhizo_opts, Rhizofs_opt_proc);
    check_debug((Rhizofs_settings_check() == 0),
            "Invalid command line arguments");

    /* set the host/socket to show in /etc/mtab */
    if (settings.host_socket != NULL) {
        if (fuse_version() >= 27) {
            snprintf(tmpbuf, TMPBUF_SIZE, "-osubtype=%.20s,fsname=%.990s", RHI_NAME_LOWER,
                    settings.host_socket);
        }
        else {
            snprintf(tmpbuf, TMPBUF_SIZE, "-ofsname=%.20s#%.990s",
                    RHI_NAME_LOWER, settings.host_socket);
        }
        fuse_opt_insert_arg(&args, 1, tmpbuf);
    }
#undef TMPBUF_SIZE

    rc = Rhizofs_fuse_main(&args);

    Rhizofs_settings_deinit();
    fuse_opt_free_args(&args);
    return rc;

error:
    Rhizofs_settings_deinit();
    fuse_opt_free_args(&args);
    return -1;
}
