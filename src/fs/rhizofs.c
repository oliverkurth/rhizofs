#include "rhizofs.h"

#include "../dbg.h"


/** private data to be stored in the fuse context */
typedef struct RhizoPriv {
    void * context;             /** the zeromq context */
} RhizoPriv;

typedef struct RhizoSettings {
    char * host_socket;  /** the name of the zmq socket to connect to */
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


/** global settings store */
static RhizoSettings settings;

static SocketPool socketpool;


/**
 * filesystem initialization
 *
 * provides:
 * - starting of background threads
 * - setting up a 0mq context
 */
static void *
Rhizofs_init(struct fuse_conn_info * UNUSED_PARAMETER(conn))
{
    RhizoPriv * priv = NULL;

    priv = calloc(sizeof(RhizoPriv), 1);
    check_mem(priv);
    priv->context = NULL;

    priv->context = zmq_init(1);
    check((priv->context != NULL), "Could not create Zmq context");

    /* create the socket pool */
    check((SocketPool_init(&socketpool, priv->context, settings.host_socket, ZMQ_REQ) == true),
            "Could not initialize the socket pool");

    return priv;

error:

    if (priv != NULL) {
        if (priv->context != NULL) {
            zmq_term(priv->context);
            priv->context = NULL;
        }

        free(priv);
        priv = NULL;
    }

    SocketPool_deinit(&socketpool);

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
Rhizofs_destroy(void * data)
{
    RhizoPriv * priv = data;

    if (priv != NULL) {
        if (priv->context != NULL) {
            zmq_term(priv->context);
            priv->context = NULL;
        }
        free(priv);
    }

    SocketPool_deinit(&socketpool);
}


/**
 * send the request and wait for a reponse
 *
 * returns NULL on error, otherwise a Reesponse the caller
 * is responsible tor free.
 */
Rhizofs__Response *
Rhizofs_communicate(Rhizofs__Request * req, int * err)
{
    void * sock = NULL;
    int rc;
    Rhizofs__Response * response = NULL;
    zmq_msg_t * msg_req = NULL;
    zmq_msg_t * msg_resp = NULL;
    struct fuse_context * fcontext = fuse_get_context();

    (*err) = 0;

    /* get a socket from the socketpool, return an errno on failure */
    sock = SocketPool_get_socket(&socketpool);
    if (sock == NULL) {
        (*err) = ENOTSOCK; /* Socket operation on non-socket */
        log_and_error("Could not fetch socket from socketpool");
    };

    msg_req = calloc(sizeof(zmq_msg_t), 1);
    check_mem(msg_req);

    if (Request_pack(req, msg_req) != true) {
        (*err) = errno;
        log_and_error("Could not pack request");
    }

    do {
        rc = zmq_send(sock, msg_req, 0);
        if (rc != 0) {
            if ((errno == EAGAIN) || (errno == EFSM)) {
                /* sleep for a short time before retrying
                 * also sleep on EFSM as the server might just be starting up
                 * with the socket no being in the correct state.
                 */
                usleep(SEND_SLEEP_USEC);

                if ((fuse_interrupted() != 0) || fuse_exited(fcontext->fuse)) {
                    (*err) = EINTR;
                    log_info("The request has been interrupted");
                    goto error;
                }
            }
            else {
                (*err) = EIO;
                log_and_error("Could not send request [errno: %d]", errno);
            }
        }
    } while (rc != 0);

    zmq_pollitem_t pollset[] = {
        { sock, 0, ZMQ_POLLIN, 0 }
    };

    msg_resp = calloc(sizeof(zmq_msg_t), 1);
    check_mem(msg_resp);

    if (zmq_msg_init(msg_resp) != 0) {
        (*err) = ENOMEM;
        log_and_error("Could not initialize response message");
    }

    rc = 1; /* set to an non-zero value to prevent exit of loop
             * before a response arrived */
    do {
        zmq_poll(pollset, 1, POLL_TIMEOUT_USEC);

        if (pollset[0].revents & ZMQ_POLLIN) {
            rc = zmq_recv(sock, msg_resp, 0);
            if (rc == 0) {  /* successfuly recieved response */
                response = Response_from_message(msg_resp);
                if (response == NULL) {
                    (*err) = EIO;
                    log_and_error("Could not unpack response");
                }
            }

            else {
                (*err) = EIO;
                log_and_error("Failed to recieve response from server");
            }
        }
        else {
            /* no response available at this time
             * check if fuse has recieved a interrupt
             * while waiting for a response
             */
            if ((fuse_interrupted() != 0) || fuse_exited(fcontext->fuse)) {
                log_info("The request has been interrupted");
                (*err) = EINTR;
                goto error;
            }
        }
    } while (rc != 0);

    /* close request after recieving relpy as it sure 0mq does not
     * hold a reference anymore */
    zmq_msg_close(msg_req);
    free(msg_req);
    zmq_msg_close(msg_resp);
    free(msg_resp);

    (*err) = Response_get_errno(response);
    return response;

error:
    if (msg_req != NULL) {
        zmq_msg_close(msg_req);
        free(msg_req);
    }
    if (msg_resp != NULL) {
        zmq_msg_close(msg_resp);
        free(msg_resp);
    }
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

#define  OP_INIT(REQ, RESP, RET_ERR)   \
    int RET_ERR = EIO; \
    Rhizofs__Request * REQ = NULL; \
    Rhizofs__Response * RESP = NULL; \
    REQ = Request_create(); \
    if (REQ == NULL ) { \
        RET_ERR = ENOMEM; \
        log_and_error("Could not create Request"); \
    }

#define OP_COMMUNICATE(REQ, RESP, RET_ERR) \
    RESP = Rhizofs_communicate(REQ, &RET_ERR); \
    check_debug((RET_ERR == 0), "Server reported an error: %d", RET_ERR); \
    check((RESP != NULL), "communicate failed");

#define OP_DEINIT(REQ, RESP) \
    Request_destroy(REQ); \
    Response_from_message_destroy(RESP);

static int
Rhizofs_readdir(const char * path, void * buf,
    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info * fi)
{
    unsigned int entry_n = 0;

    (void) offset;
    (void) fi;

    OP_INIT(request, response, returned_err);

    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__READDIR;

    OP_COMMUNICATE(request, response, returned_err)

    for (entry_n=0; entry_n<response->n_directory_entries; ++entry_n) {
        if (filler(buf, response->directory_entries[entry_n], NULL, 0)) {
            break;
        }
    }

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_getattr(const char *path, struct stat *stbuf)
{
    OP_INIT(request, response, returned_err);

    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__GETATTR;

    OP_COMMUNICATE(request, response, returned_err)
    check((response->attrs != NULL), "Response did not contain attrs");

    check((Rhizofs_convert_attrs_stat(response->attrs, stbuf) == true),
            "could not convert attrs");

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_rmdir(const char * path)
{
    OP_INIT(request, response, returned_err);

    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__RMDIR;

    OP_COMMUNICATE(request, response, returned_err)

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

    request->requesttype = RHIZOFS__REQUEST_TYPE__MKDIR;
    request->path = (char *)path;

    debug("mkdir mode: %d", (int)mode);
    request->permissions = Permissions_create((mode_t)mode);
    check((request->permissions != NULL), "Could not create access permissions struct");

    // TODO: filetype needed ??

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

    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__UNLINK;

    OP_COMMUNICATE(request, response, returned_err)

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

    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__ACCESS;

    request->permissions = Permissions_create((mode_t)mask);
    check((request->permissions != NULL), "Could not create access permissions struct");

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

    request->requesttype = RHIZOFS__REQUEST_TYPE__OPEN;
    request->path = (char *)path;

    request->openflags = OpenFlags_from_bitmask(fi->flags);
    check((request->openflags != NULL), "could not create openflags for request");

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

    request->requesttype = RHIZOFS__REQUEST_TYPE__CREATE;
    request->path = (char *)path;

    request->permissions = Permissions_create(create_mode);
    check((request->permissions != NULL), "Could not create create permissions struct");

    OP_COMMUNICATE(request, response, returned_err)

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

    return 0;
}


static int
Rhizofs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    (void) path;
    (void) isdatasync;
    (void) fi;

    return 0;
}
*/

static int
Rhizofs_read(const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    int size_read = 0;

    (void) fi;

    OP_INIT(request, response, returned_err);

    request->path = (char *)path;
    request->has_size = 1;
    request->size = (int)size;
    request->has_offset = 1;
    request->offset = (int)offset;
    request->requesttype = RHIZOFS__REQUEST_TYPE__READ;

    OP_COMMUNICATE(request, response, returned_err)
    check((Response_has_data(response) != 0), "Server did send no data in response");

    size_read = DataBlock_get_data_noalloc(response->datablock, (uint8_t *)buf, size);
    check((size_read > 0), "Could not read data");

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

    request->path = (char *)path;
    request->has_size = 1;
    request->size = (int)size;
    request->has_offset = 1;
    request->offset = (int)offset;
    request->requesttype = RHIZOFS__REQUEST_TYPE__WRITE;
    check((Request_set_data(request, (const uint8_t *) buf, (size_t)size) == true),
            "could not set request data");

    OP_COMMUNICATE(request, response, returned_err)
    check((response->has_size == 1), "response did not contain the number of bytes written");

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

    request->path = (char *)path;
    request->offset = (int)offset;
    request->has_offset = 1;
    request->requesttype = RHIZOFS__REQUEST_TYPE__TRUNCATE;

    OP_COMMUNICATE(request, response, returned_err)

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

    request->requesttype = RHIZOFS__REQUEST_TYPE__CHMOD;
    request->path = (char *)path;

    request->permissions = Permissions_create(access_mode);
    check((request->permissions != NULL), "Could not create chmod permissions struct");

    OP_COMMUNICATE(request, response, returned_err)

    OP_DEINIT(request, response)
    return 0;

error:
    OP_DEINIT(request, response)
    return -returned_err;
}


static int
Rhizofs_utimens(const char * path, const struct timespec tv[2])
{
    (void) path;
    (void) tv;
    (void) path;

    log_warn("UTIMENS is not implemented");
    return -ENOTSUP;
}

static int
Rhizofs_readlink(const char * path, char * link_target, size_t len)
{
    (void) len;
    (void) path;
    (void) link_target;

    log_warn("READLINK is not (yet) supported");
    return -ENOTSUP;
}

static int
Rhizofs_symlink(const char * path_from, const char * path_to)
{
    (void) path_from;
    (void) path_to;

    log_warn("SYMLINK is not (yet) supported");
    return -ENOTSUP;
}


static int
Rhizofs_link(const char * path_from, const char * path_to)
{
    (void) path_from;
    (void) path_to;

    log_warn("LINK is not (yet) supported");
    return -ENOTSUP;
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
    (void) path;
    (void) svfs;

    log_warn("STATFS is not (yet) supported");
    return -ENOTSUP;
}


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
//  stubs to implement
    .utimens    = Rhizofs_utimens,
    .readlink   = Rhizofs_readlink,
    .symlink    = Rhizofs_symlink,
    .link       = Rhizofs_link,
    .chmod      = Rhizofs_chmod,
    .chown      = Rhizofs_chown,
    .statfs     = Rhizofs_statfs
/*
    .release    = Rhizofs_release,
    .fsync      = Rhizofs_fsync,
*/
};



/*******************************************************************/
/* general methods                                                 */
/*******************************************************************/


void
Rhizofs_usage(const char * progname)
{
    fprintf(stderr,
        "usage: %s SOCKET MOUNTPOINT [options]\n"
        "\n"
        "general options:\n"
        "    -h   --help      print help\n"
        "    -V   --version   print version\n"
        "\n", progname
    );
}


int
Rhizofs_fuse_main(struct fuse_args *args)
{
    return fuse_main(args->argc, args->argv, &rhizofs_operations, NULL);
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
            Rhizofs_usage(outargs->argv[0]);
            fuse_opt_add_arg(outargs, "-ho");
            Rhizofs_fuse_main(outargs);
            exit(1);

        case KEY_VERSION:
            fprintf(stderr, "%s version %s\n", RHI_NAME, RHI_VERSION_FULL);
            fuse_opt_add_arg(outargs, "--version");
            Rhizofs_fuse_main(outargs);
            exit(0);

        case FUSE_OPT_KEY_NONOPT:
            if (!settings.host_socket) {
                settings.host_socket = strdup(arg); /* TODO: free this + handle failure */
                return 0;
            }
            return 1;
    }
    return 1;
}


/**
 * check the settings from the command line arguments
 * returns -1 o failure, 0 on correct arguments
 */
int
Rhizofs_check_settings()
{
    if (settings.host_socket == NULL) {
        fprintf(stderr, "Missing host");
        goto error;
    }
    return 0;

error:
    return -1;
}



int
Rhizofs_run(int argc, char * argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    char tmpbuf[1024];
    int rc;

    memset(&settings, 0, sizeof(settings));

    fuse_opt_parse(&args, &settings, rhizo_opts, Rhizofs_opt_proc);
    check_debug((Rhizofs_check_settings() == 0), "Invalid command line arguments");

    /* set the host/socket to show in /etc/mtab */
    if (settings.host_socket != NULL) {
        if (fuse_version() >= 27) {
            sprintf(tmpbuf, "-osubtype=%.20s,fsname=%.990s", RHI_NAME_LOWER,
                    settings.host_socket);
        }
        else {
            sprintf(tmpbuf, "-ofsname=%.20s#%.990s", RHI_NAME_LOWER, settings.host_socket);
        }
        fuse_opt_insert_arg(&args, 1, tmpbuf);
    }

    rc = Rhizofs_fuse_main(&args);

    fuse_opt_free_args(&args);
    return rc;

error:

    fuse_opt_free_args(&args);
    return -1;
}
