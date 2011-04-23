#include "rhizofs.h"

typedef struct RhizoConfig {
    char * socket_name; /** the name of the zmq socket to connect to */    
} RhizoConfig;

/** private data */
typedef struct RhizoPriv {
    void * context; /** the zeromq context */
    void * socket; /** the zeromq socket */
    RhizoConfig * config;
} RhizoPriv;



static void *
Rhizofs_init(struct fuse_conn_info * UNUSED_PARAMETER(conn))
{
    RhizoPriv * priv;

    priv = calloc(sizeof(RhizoPriv), 1);
    check_mem(priv);
    priv->socket = NULL;
    priv->context = NULL;
    
    priv->context = zmq_init(1);
    check((priv->context != NULL), "Could not create Zmq context");

    priv->socket = zmq_socket(priv->context, ZMQ_REQ);
    check((priv->socket != NULL), "Could not create Zmq socket");
    check((zmq_connect(priv->socket, "tcp://0.0.0.0:11555") == 0), "could not bind to socket"); // TODO: remove hardcoded socket name

    return priv;

error:

    if (priv->socket != NULL) {
        zmq_close(priv->socket);
        priv->socket = NULL;
    }

    if (priv->context != NULL) {
        zmq_term(priv->context);
        priv->context = NULL;
    }

    free(priv);
    priv = NULL;

    /* exiting here is the last fallback when
       setting up the socket fails. see the NOTES
       file 
    */
    fuse_exit(fuse_get_context()->fuse);
    return NULL;
}

static void
Rhizofs_destroy(void * data)
{
    RhizoPriv * priv = data;

    if (priv != NULL) {
        if (priv->socket != NULL) {
            zmq_close(priv->socket);
            priv->socket = NULL;
        }

        if (priv->context != NULL) {
            zmq_term(priv->context);
            priv->context = NULL;
        }
        free(priv);
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
	.readdir	= Rhizofs_readdir,
    .init       = Rhizofs_init,
    .destroy    = Rhizofs_destroy,
};


int
Rhizofs_run(int argc, char * argv[])
{
	return fuse_main(argc, argv, &rhizofs_oper, NULL);
}
