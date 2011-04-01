#ifndef __fs_broker_h__
#define __fs_broker_h__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <zmq.h>

#include "dbg.h"

/** the zmq sockets the broker is listening on */
static void * remote_socket = NULL;
static void * fuse_socket = NULL;

int Broker_init(const char * remote_socket, const char * fuse_socket);
void Broker_destroy();

#endif /* __fs_broker_h__ */
