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


#define RHIZOFS_OPT(t, p, v) { t, offsetof(RhizoSettings, p), v }

static struct fuse_opt rhizo_opts[] = {
    FUSE_OPT_KEY("-V",             KEY_VERSION),
    FUSE_OPT_KEY("--version",      KEY_VERSION),
    FUSE_OPT_KEY("-h",             KEY_HELP),
    FUSE_OPT_KEY("--help",         KEY_HELP),
    FUSE_OPT_END
};


/** global settings store */
static RhizoSettings settings;

static SocketPool socketpool;


#define  FUSE_OP_HEAD   int returned_err = EIO; \
    Rhizofs__Request * request = NULL; \
    Rhizofs__Response * response = NULL;


#define CREATE_REQUEST(R) R = Request_create(); \
    if (R == NULL ) { \
        log_err("Could not create Request"); \
        returned_err = ENOMEM; \
        goto error; \
    }




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
    int rc;

    priv = calloc(sizeof(RhizoPriv), 1);
    check_mem(priv);
    priv->context = NULL;

    priv->context = zmq_init(1);
    check((priv->context != NULL), "Could not create Zmq context");

    /* create the socket pool */
    rc = SocketPool_init(&socketpool, priv->context, settings.host_socket, ZMQ_REQ);
    check((rc == 0), "Could not initialize the socket pool");

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
    zmq_msg_t msg_req;
    zmq_msg_t msg_resp;
    struct fuse_context * fcontext = fuse_get_context();

    (*err) = 0;

    /* get a socket from the socketpool, return an errno on failure */
    sock = SocketPool_get_socket(&socketpool);
    if (sock == NULL) {
        log_err("Could not fetch socket from socketpool");
        (*err) = ENOTSOCK; /* Socket operation on non-socket */
        goto error;
    };

    if (Request_pack(req, &msg_req) != 0) {
        log_err("Could not pack request");
        (*err) = errno;
        goto error;
    }

    do {
        rc = zmq_send(sock, &msg_req, 0);

        if (rc != 0) {
            if ((errno == EAGAIN) || (errno == EFSM)) {
                /* sleep for a short time before retiying
                 * also sleep on EFSM as the server might just be starting up
                 * with the socket no being in the correct state.
                 */
                usleep(SEND_SLEEP_USEC);

                if ((fuse_interrupted() != 0) || fuse_exited(fcontext->fuse)) {
                    log_info("The request has been interrupted");
                    (*err) = EINTR;
                    goto error;
                }
            }
            else {
                log_err("Could not send request [errno: %d]", errno);
                (*err) = EIO;
                goto error;
            }
        }
    } while (rc != 0);

    zmq_pollitem_t pollset[] = {
        { sock, 0, ZMQ_POLLIN, 0 }
    };

    if (zmq_msg_init(&msg_resp) != 0) {
        log_err("Could not initialize response message");
        (*err) = ENOMEM;
        goto error;
    }

    rc = 1; /* set to an non-zero value to prevent exit of loop
             * before a response arrived */
    do {
        zmq_poll(pollset, 1, POLL_TIMEOUT_USEC);

        if (pollset[0].revents & ZMQ_POLLIN) {
            rc = zmq_recv(sock, &msg_resp, 0);
            if (rc == 0) {  /* successfuly recieved response */
                response = Response_from_message(&msg_resp);
                if (response == NULL) {
                    log_err("Could not unpack response");
                    (*err) = EIO;
                    goto error;
                }
            }

            else {
                log_err("Failed to recieve response from server");
                (*err) = EIO;
                goto error;
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
    zmq_msg_close(&msg_req);

    zmq_msg_close(&msg_resp);
    (*err) = Response_get_errno(response);
    return response;

error:
    zmq_msg_close(&msg_resp);
    zmq_msg_close(&msg_req);
    return NULL;
}



/**
 * convert a Rhizofs_Attrs struct to a stat
 * the stat has to be allocated
 * by the caller
 */
void
Rhizofs_convert_attrs_stat(Rhizofs__Attrs * attrs, struct stat * stbuf)
{
    struct fuse_context * fcontext = fuse_get_context();

    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_size = attrs->size;
    stbuf->st_mode = mapping_mode_from_protocol(attrs->modemask, 1);
    stbuf->st_atime  = attrs->atime;
    stbuf->st_ctime  = attrs->ctime;
    stbuf->st_mtime  = attrs->mtime;
    stbuf->st_nlink = 1;

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
}


/*******************************************************************/
/* filesystem methods                                              */
/*******************************************************************/

static int
Rhizofs_readdir(const char * path, void * buf,
    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info * fi)
{
    FUSE_OP_HEAD;

    unsigned int entry_n = 0;

    (void) offset;
    (void) fi;

    CREATE_REQUEST(request);
    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__READDIR;

    response = Rhizofs_communicate(request, &returned_err);
    check((returned_err == 0), "Server reported an error");
    check((response != NULL), "communicate failed");

    Request_destroy(request);

    for (entry_n=0; entry_n<response->n_directory_entries; ++entry_n) {
        if (filler(buf, response->directory_entries[entry_n], NULL, 0)) {
            break;
        }
    }

    Response_from_message_destroy(response);
    return 0;

error:
    Response_from_message_destroy(response);
    Request_destroy(request);
    return -returned_err;
}


static int
Rhizofs_getattr(const char *path, struct stat *stbuf)
{
    FUSE_OP_HEAD;

    CREATE_REQUEST(request);
    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__GETATTR;

    response = Rhizofs_communicate(request, &returned_err);
    check((returned_err == 0), "Server reported an error");
    check((response != NULL), "communicate failed");
    check((response->attrs != NULL), "Response did not contain attrs");

    Request_destroy(request);

    Rhizofs_convert_attrs_stat(response->attrs, stbuf);

    Response_from_message_destroy(response);
    return 0;

error:
    Response_from_message_destroy(response);
    Request_destroy(request);
    return -returned_err;
}


static int
Rhizofs_rmdir(const char * path)
{
    FUSE_OP_HEAD;

    CREATE_REQUEST(request);
    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__RMDIR;

    response = Rhizofs_communicate(request, &returned_err);
    check((returned_err == 0), "Server reported an error");
    check((response != NULL), "communicate failed");

    Request_destroy(request);
    Response_from_message_destroy(response);
    return 0;

error:
    Response_from_message_destroy(response);
    Request_destroy(request);
    return -returned_err;
}


static int
Rhizofs_mkdir(const char * path, mode_t mode)
{
    FUSE_OP_HEAD;

    CREATE_REQUEST(request);
    request->path = (char *)path;
    request->modemask = mapping_mode_to_protocol(mode, 1);
    request->has_modemask = 1;
    request->requesttype = RHIZOFS__REQUEST_TYPE__MKDIR;

    response = Rhizofs_communicate(request, &returned_err);
    check((returned_err == 0), "Server reported an error");
    check((response != NULL), "communicate failed");

    Request_destroy(request);
    Response_from_message_destroy(response);
    return 0;

error:
    Response_from_message_destroy(response);
    Request_destroy(request);
    return -returned_err;
}


static int
Rhizofs_unlink(const char * path)
{
    FUSE_OP_HEAD;

    CREATE_REQUEST(request);
    request->path = (char *)path;
    request->requesttype = RHIZOFS__REQUEST_TYPE__UNLINK;

    response = Rhizofs_communicate(request, &returned_err);
    check((returned_err == 0), "Server reported an error");
    check((response != NULL), "communicate failed");

    Request_destroy(request);
    Response_from_message_destroy(response);
    return 0;

error:
    Response_from_message_destroy(response);
    Request_destroy(request);
    return -returned_err;
}


static int
Rhizofs_access(const char * path, int mask)
{
    FUSE_OP_HEAD;

    CREATE_REQUEST(request);
    request->path = (char *)path;
    request->modemask = mapping_mode_to_protocol((mode_t)mask, 0);
    request->has_modemask = 1;
    request->requesttype = RHIZOFS__REQUEST_TYPE__ACCESS;

    response = Rhizofs_communicate(request, &returned_err);
    check((returned_err == 0), "Server reported an error");
    check((response != NULL), "communicate failed");

    Request_destroy(request);
    Response_from_message_destroy(response);
    return 0;

error:
    Response_from_message_destroy(response);
    Request_destroy(request);
    return -returned_err;
}


static int
Rhizofs_open(const char * path, struct fuse_file_info *fi)
{
    FUSE_OP_HEAD;

    CREATE_REQUEST(request);
    request->path = (char *)path;
    request->modemask = mapping_openflags_to_protocol(fi->flags);
    request->has_openflags = 1;
    request->requesttype = RHIZOFS__REQUEST_TYPE__OPEN;

    response = Rhizofs_communicate(request, &returned_err);
    check((returned_err == 0), "Server reported an error");
    check((response != NULL), "communicate failed");

    Request_destroy(request);
    Response_from_message_destroy(response);
    return 0;

error:
    Response_from_message_destroy(response);
    Request_destroy(request);
    return -returned_err;
}


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


static int
Rhizofs_read(const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    FUSE_OP_HEAD;

    int size_read = 0;

    (void) fi;

    CREATE_REQUEST(request);
    request->path = (char *)path;
    request->has_read_size = 1;
    request->read_size = (int)size;
    request->has_offset = 1;
    request->offset = (int)offset;
    request->requesttype = RHIZOFS__REQUEST_TYPE__READ;

    response = Rhizofs_communicate(request, &returned_err);
    check((returned_err == 0), "Server reported an error");
    check((response != NULL), "communicate failed");
    check((response->has_data != 0), "Server did send no data in response");

    debug("read %d bytes of data from server", (int)response->data.len);
    memcpy(buf, (char*)response->data.data, response->data.len);
    size_read = response->data.len;

    Request_destroy(request);
    Response_from_message_destroy(response);
    return size_read;

error:
    Response_from_message_destroy(response);
    Request_destroy(request);
    return -returned_err;

}

static struct fuse_operations rhizofs_oper = {
    .readdir    = Rhizofs_readdir,
    .init       = Rhizofs_init,
    .destroy    = Rhizofs_destroy,
    .getattr    = Rhizofs_getattr,
    .mkdir      = Rhizofs_mkdir,
    .rmdir      = Rhizofs_rmdir,
    .unlink     = Rhizofs_unlink,
    .access     = Rhizofs_access,
    .open       = Rhizofs_open,
    .release    = Rhizofs_release,
    .fsync      = Rhizofs_fsync,
    .read       = Rhizofs_read,
};



/*******************************************************************/
/* general methods                                                 */
/*******************************************************************/


void
Rhizofs_usage(const char * progname)
{
    fprintf(stderr,
        "usage: %s socket mountpoint [options]\n"
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
    return fuse_main(args->argc, args->argv, &rhizofs_oper, NULL);
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
            sprintf(tmpbuf, "-osubtype=%.20s,fsname=%.990s", RHI_NAME_LOWER, settings.host_socket);
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
