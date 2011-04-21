#ifndef __server_action_h__
#define __server_action_h__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "response.h"
#include "proto/rhizofs.pb-c.h"


int action_readdir(Rhizofs__Response **resp, const char* path);

#endif // __server_action_h__

