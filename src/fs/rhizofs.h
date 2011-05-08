#ifndef __fs_rhizofs_h__
#define __fs_rhizofs_h__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "../mapping.h"
#include "../request.h"
#include "../response.h"
#include "socketpool.h"
#include "../version.h"

// use the 2.6 fuse api
#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif
#include <fuse.h>

#include <zmq.h>

#define INTERNAL_SOCKET_NAME "inproc://fuse"

/* response timeout after which interrupts will be checked */
#define POLL_TIMEOUT_USEC 10000

#define SEND_SLEEP_USEC 1

int Rhizofs_run(int argc, char * argv[]);

#endif /* __fs_rhizofs_h__ */
