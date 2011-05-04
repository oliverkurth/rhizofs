#include "rhizofs.h"
#include "socketpool.h"

/**
 * private data
 *
 * to be stored in the fuse context
 */
typedef struct RhizoPriv {
    void * context;             /** the zeromq context */
    pthread_t broker_thread;
} RhizoPriv;

typedef struct RhizoSettings {
    char * host_socket;  /** the name of the zmq socket to connect to */
} RhizoSettings;


/**
 * enumerations for commandline options
 */
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

/** get a socket from the socketpool, return an errno on failure*/
#define GET_SOCKET(S) S = SocketPool_get_socket(&socketpool); \
    if (S == NULL) { \
        log_err("Could not fetch socket from socketpool"); \
        return -ENOTSOCK; /* Socket operation on non-socket */ \
    };


/**
 * broker function for a broker thread
 */
void *
Rhizofs_broker(void * data)
{
    RhizoPriv * priv = (RhizoPriv *) data;

    debug("Starting broker");
    Broker_run(priv->context, settings.host_socket, INTERNAL_SOCKET_NAME);
    debug("Exiting broker");

    pthread_exit(NULL);
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

    // start up the broker thread
    pthread_create(&(priv->broker_thread), NULL, Rhizofs_broker, priv);

    // create the socket pool
    rc = SocketPool_init(&socketpool, priv->context, INTERNAL_SOCKET_NAME, ZMQ_REQ);
    check((rc == 0), "Could not initialize the socket pool");

    return priv;

error:

    if (priv != NULL) {
        if (priv->context != NULL) {
            zmq_term(priv->context);
            priv->context = NULL;
        }

        // thread will exit after destrying the zmq-context
        if (priv->broker_thread) {
            pthread_join(priv->broker_thread, NULL);
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

        // thread will exit after destrying the zmq-context
        if (priv->broker_thread) {
            pthread_join(priv->broker_thread, NULL);
        }
        free(priv);
    }

    SocketPool_deinit(&socketpool);
}




/*******************************************************************/
/* filesystem methods                                              */
/*******************************************************************/

static int
Rhizofs_readdir(const char * path, void * buf,
    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info * fi)
{
    void * sock = NULL;
    GET_SOCKET(sock);

    return -EIO;
}


static struct fuse_operations rhizofs_oper = {
    .readdir    = Rhizofs_readdir,
    .init       = Rhizofs_init,
    .destroy    = Rhizofs_destroy,
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
Rhizofs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
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
                settings.host_socket = strdup(arg); // TODO: free this + handle failure
                return 0;
            }
            return 1;
    }
    return 1;
}


/**
 * check the settings from the command line arguments
 *
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
    int rc;

    memset(&settings, 0, sizeof(settings));

    fuse_opt_parse(&args, &settings, rhizo_opts, Rhizofs_opt_proc);
    check_debug((Rhizofs_check_settings() == 0), "Invalid command line arguments");

    rc = Rhizofs_fuse_main(&args);

    fuse_opt_free_args(&args);
    return rc;

error:

    fuse_opt_free_args(&args);
    return -1;
}
