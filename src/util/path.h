#ifndef __util_path_h__
#define __util_path_h__

#include <string.h>
#include <stdlib.h>

#include "dbg.h"

#define PATH_SEP '/'

int path_join(const char *, const char *, char **);

#endif // __util_path_h__
