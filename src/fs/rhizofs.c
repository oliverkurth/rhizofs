#include "rhizofs.h"

/**
 * private data
 *
 * to be stored in the fuse context
 */
typedef struct RhizoPriv {
    void * context;             /** the zeromq context */
    pthread_t broker_thread;
    char * remote_socket_name;  /** the name of the zmq socket to connect to */
} RhizoPriv;


/**
 * broker function for a broker thread
 */
void *
Rhizofs_broker(void * data)
{
    RhizoPriv * priv = (RhizoPriv *) data;

    debug("Starting broker");
    Broker_run(priv->context, "tcp://0.0.0.0:11555", INTERNAL_SOCKET_NAME); // TODO:remove hardcoded socket
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

    priv = calloc(sizeof(RhizoPriv), 1);
    check_mem(priv);
    priv->context = NULL;

    priv->context = zmq_init(1);
    check((priv->context != NULL), "Could not create Zmq context");

    // start up the broker thread
    pthread_create(&(priv->broker_thread), NULL, Rhizofs_broker, priv);


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
}


/*******************************************************************/
/* socket handling                                                 */
/*******************************************************************/

static void *
Rhizofs_socket_get()
{
    void * sock = NULL;

    // get the fuse_context for the zmq_context
    RhizoPriv * priv = (RhizoPriv *)fuse_get_context()->private_data;

    sock = zmq_socket(priv->context, ZMQ_REQ);
    check((sock != NULL), "Could not create Zmq socket");
    check((zmq_connect(sock, INTERNAL_SOCKET_NAME) == 0), "could not bind to socket"); // TODO: remove hardcoded socket name

    return sock;

error:

    if (sock != NULL) {
        zmq_close(sock);
    }

    return NULL;
}

static void
Rhizofs_socket_close(void * sock)
{
    if (sock != NULL) {
        debug("closing socket");
        zmq_close(sock);
    }
}



/*******************************************************************/
/* filesystem methods                                              */
/*******************************************************************/

static int
Rhizofs_readdir(const char * path, void * buf,
    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info * fi)
{
    return -EIO;
}


static struct fuse_operations rhizofs_oper = {
    .readdir    = Rhizofs_readdir,
    .init       = Rhizofs_init,
    .destroy    = Rhizofs_destroy,
};


int
Rhizofs_run(int argc, char * argv[])
{
    return fuse_main(argc, argv, &rhizofs_oper, NULL);
}
