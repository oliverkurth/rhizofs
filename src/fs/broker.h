#ifndef __fs_broker_h__
#define __fs_broker_h__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <zmq.h>

int Broker_run(void * context, const char * remote_socket_name, const char * internal_socket_name);

#endif /* __fs_broker_h__ */
