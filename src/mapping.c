#include "mapping.h"
#include "dbg.h"

#include <stdlib.h>


typedef struct mode_pair {
    unsigned int protocol;
    mode_t local;
} mode_pair;

typedef struct flag_pair {
    int protocol;
    int local;
} flag_pair;


static mode_pair mode_map_filetype[] = {
    { RHI_FILETYPE_DIR,     S_IFDIR },
    { RHI_FILETYPE_CHR,     S_IFCHR },
    { RHI_FILETYPE_BLK,     S_IFBLK },
    { RHI_FILETYPE_REG,     S_IFREG },
    { RHI_FILETYPE_IFO,     S_IFIFO },
    { RHI_FILETYPE_LNK,     S_IFLNK },
    { RHI_FILETYPE_SOCK,    S_IFSOCK }
};

static mode_pair mode_map_perm[] = {
    { RHI_PERM_RUSR,        S_IRUSR },
    { RHI_PERM_WUSR,        S_IWUSR },
    { RHI_PERM_XUSR,        S_IXUSR },
    { RHI_PERM_RGRP,        S_IRGRP },
    { RHI_PERM_WGRP,        S_IWGRP },
    { RHI_PERM_XGRP,        S_IXGRP },
    { RHI_PERM_ROTH,        S_IROTH },
    { RHI_PERM_WOTH,        S_IWOTH },
    { RHI_PERM_XOTH,        S_IXOTH }
};

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

#define mode_map_len(mm) (sizeof(mm)/sizeof(mode_pair))
#define flag_map_len(em) (sizeof(em)/sizeof(flag_pair))

// Prototypes
Rhizofs__PermissionSet * PermissionSet_create();
void PermissionSet_destroy(Rhizofs__PermissionSet * permset);

unsigned int
mapping_mode_to_protocol(mode_t mode, int include_filetype)
{
    unsigned int md = 0000;
    unsigned int i = 0;

    if (include_filetype) {
        for (i=0; i<mode_map_len(mode_map_filetype); ++i) {
            if (mode_map_filetype[i].local & mode) {
                md |= mode_map_filetype[i].protocol;
                break; /* break to avoid overwrite with other matching flags */
            }
        }

        if (md == 0000) {
            md |= RHI_FILETYPE_REG; /* fallback to regular file */
        }
    }

    for (i=0; i<mode_map_len(mode_map_perm); ++i) {
        if (mode_map_perm[i].local & mode) {
            md |= mode_map_perm[i].protocol;
        }
    }
    return md;
};


mode_t
mapping_mode_from_protocol(unsigned int md, int include_filetype)
{
    mode_t mode = 0;
    unsigned int i = 0;

    if (include_filetype) {
        for (i=0; i<mode_map_len(mode_map_filetype); ++i) {
            if (mode_map_filetype[i].protocol & md) {
                mode |= mode_map_filetype[i].local;
                break; /* break to avoid overwrite with other matching flags */
            }
        }

        if (mode == 0) {
            mode |= S_IFREG; /* fallback to regular file */
        }
    }

    for (i=0; i<mode_map_len(mode_map_perm); ++i) {
        if (mode_map_perm[i].protocol & md) {
            mode |= mode_map_perm[i].local;
        }
    }
    return mode;
};


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


Rhizofs__Permissions *
Permissions_create(const mode_t stat_result)
{
    Rhizofs__Permissions * permissions = NULL;

    permissions = calloc(sizeof(Rhizofs__Permissions), 1);
    check_mem(permissions);

    rhizofs__permissions__init(permissions);

    permissions->owner = NULL;
    permissions->owner = PermissionSet_create();
    check((permissions->owner != NULL), "failed to initialize owner permissionset");

    permissions->group = NULL;
    permissions->group = PermissionSet_create();
    check((permissions->group != NULL), "failed to initialize group permissionset");

    permissions->world = NULL;
    permissions->world = PermissionSet_create();
    check((permissions->world != NULL), "failed to initialize world permissionset");

    // owner
    stat_result & S_IRUSR ? permissions->owner->read = 1 : 0;
    stat_result & S_IWUSR ? permissions->owner->write = 1 : 0;
    stat_result & S_IXUSR ? permissions->owner->execute = 1 : 0;

    // group
    stat_result & S_IRGRP ? permissions->group->read = 1 : 0;
    stat_result & S_IWGRP ? permissions->group->write = 1 : 0;
    stat_result & S_IXGRP ? permissions->group->execute = 1 : 0;

    // world
    stat_result & S_IROTH ? permissions->world->read = 1 : 0;
    stat_result & S_IWOTH ? permissions->world->write = 1 : 0;
    stat_result & S_IXOTH ? permissions->world->execute = 1 : 0;

    return permissions;

error:

    Permissions_destroy(permissions);
    return NULL;
}


void
Permissions_destroy(Rhizofs__Permissions * permissions)
{
    if (permissions != NULL) {
        PermissionSet_destroy(permissions->owner);
        PermissionSet_destroy(permissions->group);
        PermissionSet_destroy(permissions->world);
        free(permissions);
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

    permissions->owner->read ? perm_bm &= S_IRUSR : 0;
    permissions->owner->write ? perm_bm &= S_IWUSR : 0;
    permissions->owner->execute ? perm_bm &= S_IXUSR : 0;

    permissions->group->read ? perm_bm &= S_IRGRP : 0;
    permissions->group->write ? perm_bm &= S_IWGRP : 0;
    permissions->group->execute ? perm_bm &= S_IXGRP : 0;

    permissions->world->read ? perm_bm &= S_IROTH : 0;
    permissions->world->write ? perm_bm &= S_IWOTH : 0;
    permissions->world->execute ? perm_bm &= S_IXOTH : 0;

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

    openflags->rdonly ? flags &= O_RDONLY : 0; 
    openflags->wronly ? flags &= O_WRONLY : 0;
    openflags->rdwr   ? flags &= O_RDWR : 0;
    openflags->creat  ? flags &= O_CREAT : 0;
    openflags->excl   ? flags &= O_EXCL : 0;
    openflags->trunc  ? flags &= O_TRUNC : 0;
    openflags->append ? flags &= O_APPEND : 0 ;

    return flags;

error:

    (*success) = false; 
    return 0;
}


void 
OpenFlags_destroy(Rhizofs__OpenFlags * openflags)
{
    free(openflags);
}
