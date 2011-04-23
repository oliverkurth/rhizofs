#ifndef __fs_rhizofs_h__
#define __fs_rhizofs_h__

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "dbg.h"

// use the 2.6 fuse api
#define FUSE_USE_VERSION 26
#include <fuse.h>

#include <zmq.h>

#define FUSE_SOCKET_NAME "inproc://fuse"

int Rhizofs_run(int argc, char * argv[]);

#endif /* __fs_rhizofs_h__ */
