#ifndef __posix_h__
#define __posix_h__

#include <grp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>


/**
 * check if the user of the program is in group gid
 * "result" will be 1 if the user is a member, otherwise 0
 *
 * returns -1 on failure. sets errno
 */
int posix_current_user_in_group(gid_t gid);

#endif /* __posix_h__ */
