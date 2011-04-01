#ifndef __serve_h__
#define __serve_h__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <zmq.h>

#include "dbg.h"
#include "proto/rhizofs.pb-c.h"

static void *context = NULL; 

int Serve_init();
int Serve_directory(const char *socket_name, const char *directory);
void Serve_destroy();

#endif // __serve_h__
