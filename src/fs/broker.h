#ifndef __fs_broker_h__
#define __fs_broker_h__

/**
 * the broker keeps the connection th the server and
 * provides a socket the threads of the filesystem can connect to
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <zmq.h>

int Broker_run(void * context, const char * remote_socket_name, const char * internal_socket_name);

#endif /* __fs_broker_h__ */
