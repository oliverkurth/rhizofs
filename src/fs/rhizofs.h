#ifndef __fs_rhizofs_h__
#define __fs_rhizofs_h__

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "../dbg.h"
#include "../version.h"
#include "broker.h"

// use the 2.6 fuse api
#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif
#include <fuse.h>

#include <zmq.h>

#define INTERNAL_SOCKET_NAME "inproc://fuse"

int Rhizofs_run(int argc, char * argv[]);

#endif /* __fs_rhizofs_h__ */
