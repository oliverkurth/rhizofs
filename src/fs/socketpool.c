#include "socketpool.h"

#include "../dbg.h"


/** destroy a single 0mq socket */
void
SocketPool_socket_destroy(void * sock)
{
    if (sock != NULL) {
        zmq_close(sock);
        sock = NULL;
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

    rc = pthread_key_create(&(socketpool->key), SocketPool_socket_destroy);
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


void
SocketPool_deinit(SocketPool * sp)
{
    if (sp) {
        if (sp->socket_name != NULL) {
            free(sp->socket_name);
        }
        if (sp->key) {
            pthread_key_delete(sp->key);
        }
    }
}


void
SocketPool_renew_socket(SocketPool * sp)
{
    check(sp != NULL, "passed socketpool is NULL");

    void * sock = pthread_getspecific(sp->key);
    if (sock != NULL) {
        zmq_close(sock);
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
        check(zmq_setsockopt(sock, ZMQ_CURVE_SERVERKEY, server_public_key, 40) == 0,
            "could not set server public key");

        if (client_public_key == NULL || client_secret_key == NULL) {
            char public_key[41];
            char secret_key[41];

            check((zmq_curve_keypair(public_key, secret_key) == 0),
                "could not create client key pair");

            check(zmq_setsockopt(sock, ZMQ_CURVE_PUBLICKEY, public_key, 40) == 0,
                "could not set client public key");
            check(zmq_setsockopt(sock, ZMQ_CURVE_SECRETKEY, secret_key, 40) == 0,
                "could not set client secret key");
        } else {
            check(zmq_setsockopt(sock, ZMQ_CURVE_PUBLICKEY, client_public_key, 40) == 0,
                "could not set client public key");
            check(zmq_setsockopt(sock, ZMQ_CURVE_SECRETKEY, client_secret_key, 40) == 0,
                "could not set client secret key");
        }
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

    check(sp != NULL, "passed socketpool is NULL");

    sock = pthread_getspecific(sp->key);
    if (sock == NULL) {

        /* create a new socket */
        sock = create_socket(sp->context, sp->socket_type,
                             sp->server_public_key,
                             sp->client_public_key, sp->client_secret_key);
        check((sock != NULL), "Could not create 0mq socket");

        check((zmq_connect(sock, sp->socket_name) == 0), "could not connect to socket");
        check((pthread_setspecific(sp->key, sock) == 0), "could not set socket in thread");
    }

    return sock;

error:
    SocketPool_socket_destroy(sock);
    return NULL;
}

