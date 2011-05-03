#ifndef __fs_socketpool_h__
#define __fs_socketpool_h__

#include <pthread.h>
#include <zmq.h>


typedef struct SocketPool {
    pthread_key_t   key;
    void * context;  /* 0mq context */
    char * socket_name;
    int socket_type;
} SocketPool;


/**
 * initialize a static socketpool struct
 * returns 0 on success or -1 on failure
 */
int SocketPool_init(SocketPool * sp, void * context, const char * socket_name, int socket_type);

/**
 * returns the 0mq socket for the current thread
 * or NULL on failure
 *
 * The socket will allready be connected to the
 * endpoint.
 */
void * SocketPool_get_socket(SocketPool * sp);

void SocketPool_deinit(SocketPool * sp);


#endif /* __fs_socketpool_h__ */
