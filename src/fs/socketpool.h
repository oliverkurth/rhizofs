#ifndef __fs_socketpool_h__
#define __fs_socketpool_h__

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <zmq.h>


void *create_socket(void *ctx, int type,
                    const char *server_public_key,
                    const char *client_public_key,
                    const char *client_secret_key);

typedef struct SocketPool {
    pthread_key_t   key;
    void * context;  /* 0mq context */
    char * socket_name;
    int socket_type;
    const char *client_public_key;
    const char *client_secret_key;
    const char *server_public_key;
} SocketPool;


/**
 * initialize a static socketpool struct
 * returns true on success or false on failure
 */
bool SocketPool_init(SocketPool * sp, void * context, const char * socket_name,
    int socket_type);

static inline
void SocketPool_set_server_public_key(SocketPool * sp, const char *key) {
    sp->server_public_key = key;
}

static inline
void SocketPool_set_client_keypair(SocketPool * sp, const char *public_key, const char *secret_key) {
    sp->client_public_key = public_key;
    sp->client_secret_key = secret_key;
}

/**
 * returns the 0mq socket for the current thread
 * or NULL on failure
 *
 * The socket will already be connected to the
 * endpoint.
 */
void * SocketPool_get_socket(SocketPool * sp);

void SocketPool_deinit(SocketPool * sp);

/**
 * destroys the socket of the current thread to
 * force creation of a new socket on the next call to
 * SocketPool_get_socket
 */
void SocketPool_renew_socket(SocketPool * sp);


#endif /* __fs_socketpool_h__ */
