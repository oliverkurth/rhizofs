#include "socketpool.h"

#include "../dbg.h"


/* the value stored via pthread_setspecific(): the socket handed to this
   thread, plus a back-pointer so the pthread-key destructor can remove it
   from the pool's tracking list again */
typedef struct ThreadSocket {
    SocketPool * pool;
    void * socket;
} ThreadSocket;


static void
SocketPool_untrack(SocketPool * sp, void * sock)
{
    pthread_mutex_lock(&sp->sockets_lock);
    SocketPoolNode ** link = &sp->sockets;
    while (*link) {
        if ((*link)->socket == sock) {
            SocketPoolNode * found = *link;
            *link = found->next;
            free(found);
            break;
        }
        link = &(*link)->next;
    }
    pthread_mutex_unlock(&sp->sockets_lock);
}


static void
SocketPool_track(SocketPool * sp, void * sock)
{
    SocketPoolNode * node = (SocketPoolNode *)malloc(sizeof(SocketPoolNode));
    check_mem(node);
    node->socket = sock;

    pthread_mutex_lock(&sp->sockets_lock);
    node->next = sp->sockets;
    sp->sockets = node;
    pthread_mutex_unlock(&sp->sockets_lock);
    return;
error:
    return;
}


/** pthread-key destructor: runs when a thread that owns a pooled socket
 *  exits while the pool itself is still alive */
static void
SocketPool_thread_destructor(void * value)
{
    ThreadSocket * ts = (ThreadSocket *)value;
    if (ts != NULL) {
        SocketPool_untrack(ts->pool, ts->socket);
        zmq_close(ts->socket);
        free(ts);
    }
}


bool
SocketPool_init(SocketPool * socketpool, void * context, const char * socket_name,
        int socket_type)
{
    int rc;

    memset(socketpool, 0, sizeof(SocketPool));

    socketpool->socket_name = strdup(socket_name);
    check_mem(socketpool->socket_name);

    socketpool->socket_type = socket_type;
    socketpool->context = context;
    socketpool->sockets = NULL;

    check((pthread_mutex_init(&(socketpool->sockets_lock), NULL) == 0),
        "pthread_mutex_init failed.");

    rc = pthread_key_create(&(socketpool->key), SocketPool_thread_destructor);
    check((rc==0), "pthread_key_create failed.");

    return true;

error:

    if (socketpool->socket_name != NULL) {
        free(socketpool->socket_name);
    }
    if (socketpool->key) {
        pthread_key_delete(socketpool->key);
    }
    return false;
}


/**
 * pthread_key_delete() does not run the key's destructor for values that
 * are still set on live threads, so without explicitly closing them here,
 * sockets handed out to (still-running) worker threads stay open. That
 * then makes zmq_ctx_term()/zmq_ctx_destroy() on the pool's context block
 * forever, since it waits for every socket created in that context to be
 * closed.
 */
void
SocketPool_deinit(SocketPool * sp)
{
    if (!sp) {
        return;
    }

    pthread_mutex_lock(&sp->sockets_lock);
    SocketPoolNode * node = sp->sockets;
    sp->sockets = NULL;
    pthread_mutex_unlock(&sp->sockets_lock);

    while (node) {
        SocketPoolNode * next = node->next;
        zmq_close(node->socket);
        free(node);
        node = next;
    }

    if (sp->socket_name != NULL) {
        free(sp->socket_name);
        sp->socket_name = NULL;
    }
    if (sp->key) {
        pthread_key_delete(sp->key);
        sp->key = 0;
    }
}


void
SocketPool_renew_socket(SocketPool * sp)
{
    check(sp != NULL, "passed socketpool is NULL");

    ThreadSocket * ts = (ThreadSocket *)pthread_getspecific(sp->key);
    if (ts != NULL) {
        SocketPool_untrack(sp, ts->socket);
        zmq_close(ts->socket);
        free(ts);
        if (pthread_setspecific(sp->key, NULL) != 0) {
            debug("could not clear socket in thread");
        }
    }
    return;
error:
    return;
}


void *create_socket(void *ctx, int type,
                    const char *server_public_key,
                    const char *client_public_key,
                    const char *client_secret_key)
{
    void * sock = NULL;

    sock = zmq_socket(ctx, type);
    check((sock != NULL), "Could not create 0mq socket");

    int hwm = 1; /* prevents memory leaks when fuse interrupts while waiting on server */
    zmq_setsockopt(sock, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(sock, ZMQ_RCVHWM, &hwm, sizeof(hwm));

#ifdef ZMQ_MAKE_VERSION
#if ZMQ_VERSION >= ZMQ_MAKE_VERSION(2,1,0)
    int linger = 0;
    zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(linger));
#endif
#endif

    /* if server_public_key is set, encryption is enabled , otherwise it's unencrypted */
    /* if encryption is enabled: if client_public_key and client_secret_key are set,
       use them. Otherwise, we generate client keys on the fly. */
    if (server_public_key != NULL) {
        check(client_public_key != NULL, "client public key is not set");
        check(client_secret_key != NULL, "client secret key is not set");

        check(zmq_setsockopt(sock, ZMQ_CURVE_SERVERKEY, server_public_key, 40) == 0,
            "could not set server public key");
        check(zmq_setsockopt(sock, ZMQ_CURVE_PUBLICKEY, client_public_key, 40) == 0,
            "could not set client public key");
        check(zmq_setsockopt(sock, ZMQ_CURVE_SECRETKEY, client_secret_key, 40) == 0,
            "could not set client secret key");
    }
    return sock;
error:
    if (sock)
        zmq_close(sock);
    return NULL;
}


void *
SocketPool_get_socket(SocketPool * sp)
{
    void * sock = NULL;
    ThreadSocket * ts = NULL;

    check(sp != NULL, "passed socketpool is NULL");

    ts = (ThreadSocket *)pthread_getspecific(sp->key);
    if (ts != NULL) {
        return ts->socket;
    }

    /* create a new socket */
    sock = create_socket(sp->context, sp->socket_type,
                         sp->server_public_key,
                         sp->client_public_key, sp->client_secret_key);
    check((sock != NULL), "Could not create 0mq socket");

    check((zmq_connect(sock, sp->socket_name) == 0), "could not connect to socket");

    ts = (ThreadSocket *)malloc(sizeof(ThreadSocket));
    check_mem(ts);
    ts->pool = sp;
    ts->socket = sock;

    check((pthread_setspecific(sp->key, ts) == 0), "could not set socket in thread");

    SocketPool_track(sp, sock);

    return sock;

error:
    if (sock != NULL) {
        zmq_close(sock);
    }
    if (ts != NULL) {
        free(ts);
    }
    return NULL;
}

