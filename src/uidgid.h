#ifndef __uidgid_h__
#define __uidgid_h__

#include <grp.h>


/**
 * check if the user of the program is in group gid
 * result will be 1 if the user is a member, otherwise 0
 *
 * returns 0 on success, -1 on failure. sets errno
 */
int uidgid_in_group(gid_t gid, int * result);

#endif /* __uidgid_h__ */
