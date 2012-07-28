#include "mapping.h"
#include "dbg.h"
#include "helpers.h"
#include "posix.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>


typedef struct mode_pair {
    unsigned int protocol;
    mode_t local;
} mode_pair;

typedef struct flag_pair {
    int protocol;
    int local;
} flag_pair;


static flag_pair errno_map[] = {
    { RHIZOFS__ERRNO__ERRNO_NONE,      0 },
    { RHIZOFS__ERRNO__ERRNO_PERM,      EPERM },
    { RHIZOFS__ERRNO__ERRNO_NOENT,     ENOENT },
    { RHIZOFS__ERRNO__ERRNO_NOMEM,     ENOMEM },
    { RHIZOFS__ERRNO__ERRNO_ACCES,     EACCES },
    { RHIZOFS__ERRNO__ERRNO_BUSY,      EBUSY },
    { RHIZOFS__ERRNO__ERRNO_EXIST,     EEXIST },
    { RHIZOFS__ERRNO__ERRNO_NOTDIR,    ENOTDIR },
    { RHIZOFS__ERRNO__ERRNO_ISDIR,     EISDIR },
    { RHIZOFS__ERRNO__ERRNO_INVAL,     EINVAL },
    { RHIZOFS__ERRNO__ERRNO_FBIG,      EFBIG },
    { RHIZOFS__ERRNO__ERRNO_NOSPC,     ENOSPC },
    { RHIZOFS__ERRNO__ERRNO_ROFS,      EROFS },
    { RHIZOFS__ERRNO__ERRNO_SPIPE,     ESPIPE },

    /* custom methods are located at the end of this list */
    { RHIZOFS__ERRNO__ERRNO_UNKNOWN,            EIO }, /* everything unknown is an IO error */
    { RHIZOFS__ERRNO__ERRNO_INVALID_REQUEST,    EINVAL },
    { RHIZOFS__ERRNO__ERRNO_UNSERIALIZABLE,     EIO }
};

#define flag_map_len(em) (sizeof(em)/sizeof(flag_pair))

// Prototypes
Rhizofs__PermissionSet * PermissionSet_create();
void PermissionSet_destroy(Rhizofs__PermissionSet * permset);
bool PermissionSet_to_string(const Rhizofs__PermissionSet * permset, char * outstr);



inline Rhizofs__PermissionSet *
PermissionSet_create()
{
    Rhizofs__PermissionSet * permset = NULL;

    permset = calloc(sizeof(Rhizofs__PermissionSet), 1);
    check_mem(permset);
    rhizofs__permission_set__init(permset);

    return permset;

error:
    free(permset);
    return NULL;
}


inline void
PermissionSet_destroy(Rhizofs__PermissionSet * permset)
{
    free(permset);
}


/**
 * creates a human readable string from the permissionset.
 *
 * outstr has to be a preallocated string of at least 4 characters
 * length
 *
 * return a string containg "EEE" on error.
 */
bool
PermissionSet_to_string(const Rhizofs__PermissionSet * permset, char * outstr)
{
    check((permset != NULL), "permset is null");

    outstr[0] = permset->read ? 'r' : '-';
    outstr[1] = permset->write ?  'w' : '-';
    outstr[2] = permset->execute ? 'x' : '-';
    outstr[3] = '\0';
    
    return true;

error:
    outstr[0] = 'E';
    outstr[1] = 'E';
    outstr[2] = 'E';
    outstr[3] = '\0';

    return false;
}


Rhizofs__Permissions *
Permissions_create(const mode_t mode)
{
    Rhizofs__Permissions * permissions = NULL;

    permissions = calloc(sizeof(Rhizofs__Permissions), 1);
    check_mem(permissions);

    rhizofs__permissions__init(permissions);

#define PS_INIT(PS_NAME) \
    permissions->PS_NAME = NULL; \
    permissions->PS_NAME = PermissionSet_create(); \
    check((permissions->PS_NAME != NULL), "failed to initialize " STRINGIFY(PS_NAME) " permissionset");

    PS_INIT(owner);
    PS_INIT(group);
    PS_INIT(world);

#undef PS_INIT

    // owner
    (mode & S_IRUSR) ? permissions->owner->read = 1 : NO_OP;
    (mode & S_IWUSR) ? permissions->owner->write = 1 : NO_OP;
    (mode & S_IXUSR) ? permissions->owner->execute = 1 : NO_OP;

    // group
    (mode & S_IRGRP) ? permissions->group->read = 1 : NO_OP;
    (mode & S_IWGRP) ? permissions->group->write = 1 : NO_OP;
    (mode & S_IXGRP) ? permissions->group->execute = 1 : NO_OP;

    // world
    (mode & S_IROTH) ? permissions->world->read = 1 : NO_OP;
    (mode & S_IWOTH) ? permissions->world->write = 1 : NO_OP;
    (mode & S_IXOTH) ? permissions->world->execute = 1 : NO_OP;

#ifdef DEBUG
    char permstr[10];
    Permissions_to_string(permissions, &permstr[0]);
    debug("Converted bitmask %d to permissions %s", (int)mode, permstr);
#endif

    return permissions;

error:

    Permissions_destroy(permissions);
    return NULL;
}

bool
Permissions_to_string(const Rhizofs__Permissions * permissions, char * outstr)
{
    check((permissions != NULL), "permissions is NULL");

    return PermissionSet_to_string(permissions->owner, &outstr[0]) && 
            PermissionSet_to_string(permissions->group, &outstr[3]) && 
            PermissionSet_to_string(permissions->world, &outstr[6]);

error:
    return false;
}

void
Permissions_destroy(Rhizofs__Permissions * permissions)
{
    if (permissions != NULL) {
        PermissionSet_destroy(permissions->owner);
        PermissionSet_destroy(permissions->group);
        PermissionSet_destroy(permissions->world);
        free(permissions);
        permissions = NULL;
    }
}

int
Permissions_to_bitmask(const Rhizofs__Permissions * permissions, bool * success)
{
    int perm_bm = 0;
    *success = true;

    check((permissions != NULL), "permissions struct is null");
    check((permissions->owner != NULL), "permissions->owner struct is null");
    check((permissions->group != NULL), "permissions->group struct is null");
    check((permissions->world != NULL), "permissions->world struct is null");


    permissions->owner->read ? perm_bm |= S_IRUSR : NO_OP;
    permissions->owner->write ? perm_bm |= S_IWUSR : NO_OP;
    permissions->owner->execute ? perm_bm |= S_IXUSR : NO_OP;

    permissions->group->read ? perm_bm |= S_IRGRP : NO_OP;
    permissions->group->write ? perm_bm |= S_IWGRP : NO_OP;
    permissions->group->execute ? perm_bm |= S_IXGRP : NO_OP;

    permissions->world->read ? perm_bm |= S_IROTH : NO_OP;
    permissions->world->write ? perm_bm |= S_IWOTH : NO_OP;
    permissions->world->execute ? perm_bm |= S_IXOTH : NO_OP;

#ifdef DEBUG
    char permstr[10];
    Permissions_to_string(permissions, &permstr[0]);
    debug("Converted permissions %s to bitmask %d", permstr, perm_bm);
#endif

    return perm_bm;

error:

    *success = false;
    return 0;
}


int
Errno_from_local(int lerrno)
{
    int perrno = RHIZOFS__ERRNO__ERRNO_UNKNOWN; /* default value */
    unsigned int i=0;

    for (i=0; i<flag_map_len(errno_map); ++i) {
        if (errno_map[i].local == lerrno) {
            perrno = errno_map[i].protocol;
            break;
        }
    }

    return perrno;
}


int
Errno_to_local(int perrno)
{
    int lerrno = EIO; /* default */
    unsigned int i=0;

    for (i=0; i<flag_map_len(errno_map); ++i) {
        if (errno_map[i].protocol == perrno) {
            lerrno = errno_map[i].local;
            break;
        }
    }

    return lerrno;
}


int
FileType_to_local(const Rhizofs__FileType filetype)
{
    int local_filetype = 0;

    switch (filetype) {
        case RHIZOFS__FILE_TYPE__DIRECTORY:
            local_filetype = S_IFDIR;
            break;
        case RHIZOFS__FILE_TYPE__CHARACTER_DEVICE:
            local_filetype = S_IFCHR;
            break;
        case RHIZOFS__FILE_TYPE__BLOCK_DEVICE:
            local_filetype = S_IFBLK;
            break;
        case RHIZOFS__FILE_TYPE__REGULAR_FILE:
            local_filetype = S_IFREG;
            break;
        case RHIZOFS__FILE_TYPE__FIFO:
            local_filetype = S_IFIFO;
            break;
        case RHIZOFS__FILE_TYPE__SYMLINK:
            local_filetype = S_IFLNK;
            break;
        case RHIZOFS__FILE_TYPE__SOCKET:
            local_filetype = S_IFSOCK;
            break;
        default:
            /* fallback to regular file */
            local_filetype = S_IFREG;
            log_warn("could not map filetype to local filetype: %d", (int)filetype);
    }
    return local_filetype;
}


Rhizofs__FileType
FileType_from_local(const mode_t stat_result)
{
    Rhizofs__FileType filetype = RHIZOFS__FILE_TYPE__REGULAR_FILE; 

    if (stat_result & S_IFDIR)       { filetype = RHIZOFS__FILE_TYPE__DIRECTORY; }
    else if (stat_result & S_IFCHR)  { filetype = RHIZOFS__FILE_TYPE__CHARACTER_DEVICE; }
    else if (stat_result & S_IFBLK)  { filetype = RHIZOFS__FILE_TYPE__BLOCK_DEVICE; }
    else if (stat_result & S_IFREG)  { filetype = RHIZOFS__FILE_TYPE__REGULAR_FILE; }
    else if (stat_result & S_IFIFO)  { filetype = RHIZOFS__FILE_TYPE__FIFO; }
    else if (stat_result & S_IFLNK)  { filetype = RHIZOFS__FILE_TYPE__SYMLINK; }
    else if (stat_result & S_IFSOCK) { filetype = RHIZOFS__FILE_TYPE__SOCKET; }
    
    return filetype;
}


Rhizofs__OpenFlags *
OpenFlags_from_bitmask(const int flags) 
{
    Rhizofs__OpenFlags * openflags = NULL;

    openflags = calloc(sizeof(Rhizofs__OpenFlags), 1);
    check_mem(openflags);

    rhizofs__open_flags__init(openflags);

    openflags->rdonly = (flags & O_RDONLY) ? 1 : 0;
    openflags->wronly = (flags & O_WRONLY) ? 1 : 0; 
    openflags->rdwr   = (flags & O_RDWR)   ? 1 : 0; 
    openflags->creat  = (flags & O_CREAT)  ? 1 : 0; 
    openflags->excl   = (flags & O_EXCL)   ? 1 : 0; 
    openflags->trunc  = (flags & O_TRUNC)  ? 1 : 0; 
    openflags->append = (flags & O_APPEND) ? 1 : 0; 

    return openflags;

error:
    free(openflags);
    return NULL;
}


int
OpenFlags_to_bitmask(const Rhizofs__OpenFlags * openflags, bool * success) 
{
    int flags = 0;
    (*success) = true; 

    check((openflags != NULL), "passed openflags struct is NULL");
    check((success != NULL), "passed pointer to success bool is NULL");

    openflags->rdonly ? flags |= O_RDONLY : NO_OP;
    openflags->wronly ? flags |= O_WRONLY : NO_OP;
    openflags->rdwr   ? flags |= O_RDWR : NO_OP;
    openflags->creat  ? flags |= O_CREAT : NO_OP;
    openflags->excl   ? flags |= O_EXCL : NO_OP;
    openflags->trunc  ? flags |= O_TRUNC : NO_OP;
    openflags->append ? flags |= O_APPEND : NO_OP ;

    return flags;

error:

    (*success) = false; 
    return 0;
}


void 
OpenFlags_destroy(Rhizofs__OpenFlags * openflags)
{
    free(openflags);
    openflags = NULL;
}


Rhizofs__Attrs *
Attrs_create(const struct stat * stat_result)
{
    Rhizofs__Attrs * attrs = NULL;

    attrs = calloc(sizeof(Rhizofs__Attrs), 1);
    check_mem(attrs);
    rhizofs__attrs__init(attrs);

    attrs->size = stat_result->st_size;

    attrs->permissions = Permissions_create((mode_t)stat_result->st_mode);
    check((attrs->permissions != NULL), "Could not create access permissions struct");

    attrs->filetype = FileType_from_local((mode_t)stat_result->st_mode);

    /* user */
    if (getuid() == stat_result->st_uid) {
        attrs->is_owner = 1;
    }
    else {
        attrs->is_owner = 0;
    }

    /* group */
    int is_in_group = posix_current_user_in_group(stat_result->st_gid);
    check((is_in_group != -1), "Could not fetch group info");
    attrs->is_in_group = is_in_group;

    /* times */
    attrs->atime = (int)stat_result->st_atime;
    attrs->mtime = (int)stat_result->st_mtime;
    attrs->ctime = (int)stat_result->st_ctime;

    return attrs;

error:

    Attrs_destroy(attrs);
    return NULL;
}


void
Attrs_destroy(Rhizofs__Attrs * attrs)
{
    if (attrs) {
        free(attrs->permissions);
        free(attrs);
        attrs = NULL;
    }
}

bool
Attrs_copy_to_stat(const Rhizofs__Attrs * attrs, struct stat * stat_result)
{
    int filetype = 0;
    int permissions = 0;
    bool success = false;

    check((attrs != NULL), "passed attrs is NULL");
    check((stat_result != NULL), "passed stat_result is NULL");

    // zero the stat
    memset(stat_result, 0, sizeof(struct stat));

    filetype = FileType_to_local(attrs->filetype);

    permissions = Permissions_to_bitmask(attrs->permissions, &success);
    check((success == true), "Could not convert permissions to bitmask");

    stat_result->st_size = attrs->size;
    stat_result->st_mode = filetype | permissions;
    stat_result->st_atime  = attrs->atime;
    stat_result->st_ctime  = attrs->ctime;
    stat_result->st_mtime  = attrs->mtime;
    stat_result->st_nlink = 1;

    return true;

error:
    return false;
}
