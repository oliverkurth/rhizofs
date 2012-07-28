#ifndef __mapping_h__
#define __mapping_h__

#include <sys/stat.h>
#include <stdbool.h>
#include <fcntl.h>
#include "proto/rhizofs.pb-c.h"


//
// Errno
//
//
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

