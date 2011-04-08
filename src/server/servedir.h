#ifndef __server_servedir_h__
#define __server_servedir_h__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h> // for PATH_MAX
#include <sys/stat.h>

#include <zmq.h>

#include "dbg.h"
#include "server/io.h"
#include "server/response.h"
#include "proto/rhizofs.pb-c.h"

typedef struct ServeDir {
    char * directory;
    char * socket_name;
    void * socket;
} ServeDir;


ServeDir * ServeDir_create(void *context, char * socket_name, char *directory);
int ServeDir_serve(ServeDir * sd);
void ServeDir_destroy(ServeDir * sd);

#endif // __server_servedir_h__
