#ifndef __mapping_h__
#define __mapping_h__

#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include "proto/rhizofs.pb-c.h"

/* filetypes */
#define RHI_FILETYPE_DIR   0040000 /* Directory */
#define RHI_FILETYPE_CHR   0020000 /* Character device */
#define RHI_FILETYPE_BLK   0060000 /* Block device */
#define RHI_FILETYPE_REG   0100000 /* Regular file */
#define RHI_FILETYPE_IFO   0010000 /* FIFO */
#define RHI_FILETYPE_LNK   0120000 /* Symbolic link */
#define RHI_FILETYPE_SOCK  0140000 /* Socket */

/* permissions */
#define RHI_PERM_RUSR 0400     /* Read by owner */
#define RHI_PERM_WUSR 0200     /* Write by owner */
#define RHI_PERM_XUSR 0100     /* Execute by owner */
#define RHI_PERM_RGRP (RHI_PERM_RUSR >> 3)  /* Read by group */
#define RHI_PERM_WGRP (RHI_PERM_WUSR >> 3)  /* Write by group */
#define RHI_PERM_XGRP (RHI_PERM_XUSR >> 3)  /* Execute by group */
#define RHI_PERM_ROTH (RHI_PERM_RGRP >> 3)  /* Read by others */
#define RHI_PERM_WOTH (RHI_PERM_WGRP >> 3)  /* Write by others */
#define RHI_PERM_XOTH (RHI_PERM_XGRP >> 3)  /* Execute by others */

/* file open flags */
#define RHI_OPEN_RDONLY         00
#define RHI_OPEN_WRONLY         01
#define RHI_OPEN_RDWR           02
#define RHI_OPEN_CREAT          0100
#define RHI_OPEN_EXCL           0200
#define RHI_OPEN_NOCTTY         0400
#define RHI_OPEN_TRUNC          01000
#define RHI_OPEN_APPEND         02000
#define RHI_OPEN_NONBLOCK       04000
#define RHI_OPEN_NDELAY         RHI_OPEN_NONBLOCK
#define RHI_OPEN_SYNC           04010000
#define RHI_OPEN_FSYNC          RHI_OPEN_SYNC
#define RHI_OPEN_ASYNC          020000



/* to_protocol = local to protocol
 * from_protocol = protocol to local
 */

/*
unsigned int mapping_mode_to_protocol(mode_t mode, int include_filetype);
mode_t mapping_mode_from_protocol(unsigned int, int include_filetype);
*/

int Errno_from_local(int lerrno);
int Errno_to_local(int perrno);


//
// Permissions
//

/**
 * create a Permissions struct from a stat mode_t
 *
 * returns NULL on failure
 */
Rhizofs__Permissions * Permissions_create(const mode_t mode);

/**
 * create a permissions bitmask from a Rhizofs__Permissions struct
 *
 * on error the parameter success will set to false
 */
int Permissions_to_bitmask(const Rhizofs__Permissions * permissions, bool * success);

/**
 * creates a human readable string from the permissions.
 *
 * outstr has to be a preallocated string of at least 10 characters
 * length
 *
 * return a string containing "EEE" on error.
 */
bool
Permissions_to_string(const Rhizofs__Permissions * permissions, char * outstr);

/**
 * free a permissions struct
 */
void Permissions_destroy(Rhizofs__Permissions * permissions);


//
// OpenFlags
//

/**
 * create and return a new OpenFlags structure
 * from flags from the open syscall
 *

 * returns NULL on error
 */
Rhizofs__OpenFlags * OpenFlags_from_bitmask(const int flags);

/**
 * convert the contents of a OpenFlags structure to a btimask
 *
 * on error the parameter success will set to false
 */
int OpenFlags_to_bitmask(const Rhizofs__OpenFlags * openflags, bool * success);

/**
 * delete and free an OpenFlags struct
 */
void OpenFlags_destroy(Rhizofs__OpenFlags * openflags);



//
// FileType
//

/**
 * convert a filetype enum to the local
 * mode_t value
 */
int FileType_to_local(const Rhizofs__FileType filetype);

/**
 * get the Rhizofs__FileType from the result of a stat call
 */
Rhizofs__FileType FileType_from_local(const mode_t stat_result);


//
// Attrs
//

/**
 * create a new attrs struct from the stat struct from
 * a call to stat
 *
 * returns NULL on error
 */
Rhizofs__Attrs * Attrs_create(const struct stat * stat_result);

/**
 * free a Attrs struct
 */
void Attrs_destroy(Rhizofs__Attrs * attrs);

/**
 * copy the contents of an attrs struct to a preallocated
 * stat struct
 *
 * this will not set the st_uid and st_gid attributes of the stat
 *
 * returns false on failure.
 */
bool Attrs_copy_to_stat(const Rhizofs__Attrs * attrs, struct stat * stat_result);


#endif /* __mapping_h__ */

