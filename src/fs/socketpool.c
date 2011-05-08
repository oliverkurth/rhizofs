#include "socketpool.h"

#include "../dbg.h"


/** destroy a single 0mq socket */
void
SocketPool_socket_destroy(void * sock)
{
    if (sock != NULL) {
        zmq_close(sock);
    }
}


int
SocketPool_init(SocketPool * socketpool, void * context, const char * socket_name,
        int socket_type)
{
    int rc;

    memset(socketpool, 0, sizeof(SocketPool)); // TODO: check

    socketpool->socket_name = strdup(socket_name);
    check_mem(socketpool->socket_name);

    socketpool->socket_type = socket_type;
    socketpool->context = context;

    rc = pthread_key_create(&(socketpool->key), SocketPool_socket_destroy);
    check((rc==0), "pthread_key_create failed.");

    return 0;

error:

    if (socketpool->socket_name != NULL) {
        free(socketpool->socket_name);
    }
    if (socketpool->key) {
        pthread_key_delete(socketpool->key);
    }
    return -1;
}


void
SocketPool_deinit(SocketPool * sp)
{
    if (sp->socket_name != NULL) {
        free(sp->socket_name);
    }
    if (sp->key) {
        pthread_key_delete(sp->key);
    }
}


void *
SocketPool_get_socket(SocketPool * sp)
{
    void * sock = NULL;

    sock = pthread_getspecific(sp->key);
    if (sock == NULL) {
        int hwm = 1; /* prevents memory leaks when fuse interrupts while waiting on server */
        int linger = 0;

        /* create a new socket */
        sock = zmq_socket(sp->context, sp->socket_type);
        check((sock != NULL), "Could not create 0mq socket");

        zmq_setsockopt(sock, ZMQ_HWM, &hwm, sizeof(hwm));
        zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(linger));

        check((zmq_connect(sock, sp->socket_name) == 0), "could not connect to socket");
        check((pthread_setspecific(sp->key, sock) == 0), "could not set socket in thread");
    }

    return sock;

error:

    if (sock != NULL) {
        zmq_close(sock);
    }
    return NULL;
}

