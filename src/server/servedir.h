#ifndef __server_servedir_h__
#define __server_servedir_h__

#ifdef linux
#define _XOPEN_SOURCE 500   /* enable pread()/pwrite() */
#endif

#include <stdbool.h>

typedef struct ServeDir {
    char * directory;
    char * socket_name;
    void * socket;
} ServeDir;


ServeDir * ServeDir_create(void *context, char * socket_name, char *directory);
bool ServeDir_serve(ServeDir * sd);
void ServeDir_destroy(ServeDir * sd);

#endif /* __server_servedir_h__ */
