#include "servedir.h"

#include <limits.h> /* for PATH_MAX */
#include <stdbool.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <fcntl.h>

#include <zmq.h>

#include "../dbg.h"
#include "../datablock.h"
#include "../path.h"
#include "../helpers.h"
#include "../response.h"
#include "../request.h"
#include "../proto/rhizofs.pb-c.h"

#if !defined PATH_MAX && defined _PC_PATH_MAX
#define PATH_MAX (pathconf ("/", _PC_PATH_MAX) < 1 ? 4096 \
            : pathconf ("/", _PC_PATH_MAX))
#endif


/**
 * check for memory and set response error on failure
 */
#define check_mem_response(A) if(!(A)) { \
    response->errnotype = RHIZOFS__ERRNO__ERRNO_NOMEM ; \
    errno=0; \
    log_and_error("Out of memory."); \
}

/**
 * upper limit for the amount of data a single READ request may ask the
 * server to buffer.
 *
 * the size of a read is chosen by the client, and the FUSE client asks
 * for at most a few hundred kilobytes at a time, so this is only here
 * to keep a client from making the server allocate arbitrary amounts of
 * memory. reads are allowed to return less than was asked for, so a
 * request above the limit is served up to it rather than refused.
 */
#define MAX_READ_SIZE ((size_t)64 * 1024 * 1024)

/* Writes are supplied entirely by a peer-controlled protobuf message.  Keep
 * their decoded size bounded just as reads are, rather than letting a tiny
 * compressed payload request an arbitrarily large allocation. */
#define MAX_WRITE_SIZE ((size_t)64 * 1024 * 1024)

/* Walking a path only requires search permission on its directories. */
#if defined(O_PATH)
#define DIRECTORY_ACCESS O_PATH
#elif defined(O_SEARCH)
#define DIRECTORY_ACCESS O_SEARCH
#else
#define DIRECTORY_ACCESS O_RDONLY
#endif

/**
 * default permissions for file creation. the same permission set is used
 * by the GNU coreutils touch command
 */
static const int default_file_creation_permissions =
        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;


// prototypes
static int ServeDir_parent(const ServeDir * sd, Rhizofs__Response * response,
        const char * reqpath, int * parentfd, char ** leaf);
static int ServeDir_op_ping(Rhizofs__Response * response);
static int ServeDir_op_invalid(Rhizofs__Response * response);
#define SERVEDIR_OP(NAME)   \
    static int ServeDir_op_ ## NAME (const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response * response);
SERVEDIR_OP(access)
SERVEDIR_OP(chmod)
SERVEDIR_OP(create)
SERVEDIR_OP(getattr)
SERVEDIR_OP(mkdir)
SERVEDIR_OP(open)
SERVEDIR_OP(read)
SERVEDIR_OP(readdir)
SERVEDIR_OP(rename)
SERVEDIR_OP(rmdir)
SERVEDIR_OP(truncate)
SERVEDIR_OP(unlink)
SERVEDIR_OP(utimens)
SERVEDIR_OP(write)
SERVEDIR_OP(link)
SERVEDIR_OP(symlink)
SERVEDIR_OP(readlink)
SERVEDIR_OP(mknod)
SERVEDIR_OP(statfs)
#undef SERVEDIR_OP



ServeDir *
ServeDir_create(void *context, char *socket_name, char *directory)
{
    ServeDir * sd = NULL;
    sd = (ServeDir *)calloc(1, sizeof(ServeDir));
    check_mem(sd);

    sd->socket = NULL;
    sd->directory = NULL;
    sd->directory_fd = -1;
    struct stat sr;

    /* get the absolute path to the directory */
    sd->directory = calloc(PATH_MAX, sizeof(char));
    check_mem(sd->directory);
    check((realpath(directory, sd->directory) != NULL),
            "Could not resolve directory path");

    /* validate the directory */
    check((stat((const char*)directory, &sr) == 0), "could not stat %s", directory);
    check(S_ISDIR(sr.st_mode), "%s is not a directory.", directory);
    sd->directory_fd = open(sd->directory, DIRECTORY_ACCESS | O_DIRECTORY | O_CLOEXEC);
    check((sd->directory_fd != -1), "could not open served directory");

    sd->socket = zmq_socket(context, ZMQ_REP);
    sd->socket_name = socket_name;
    check((sd->socket != NULL), "Could not create zmq socket");
    check((zmq_connect(sd->socket, socket_name) == 0),
            "could not bind to socket %s", socket_name);

    return sd;

error:

    if (sd != NULL) {
        if (sd->directory != NULL) {
            free(sd->directory); // free the memory allocated by realpath
        }
        if (sd->socket) {
            zmq_close(sd->socket);
        }
        if (sd->directory_fd != -1) {
            close(sd->directory_fd);
        }
        free(sd);
    }
    return NULL;
}


void
ServeDir_destroy(ServeDir * sd)
{
    if (sd) {
        // free the memory allocated by realpath
        free(sd->directory);
        if (sd->socket != NULL) {
            zmq_close(sd->socket);
            sd->socket = NULL;
        }
        if (sd->directory_fd != -1) {
            close(sd->directory_fd);
            sd->directory_fd = -1;
        }
    }
    free(sd);
}


bool
ServeDir_serve(ServeDir * sd)
{
    zmq_msg_t msg_req;
    zmq_msg_t msg_rep;
    int term_loop = 0;
    Rhizofs__Request *request = NULL;
    Rhizofs__Response *response = NULL;

    debug("Serving directory <%s> on <%s>", sd->directory, sd->socket_name);

    while (term_loop == 0) {
        int zmq_rc = 0;

        check((zmq_msg_init(&msg_req) == 0), "Could not initialize request message");

        zmq_rc = zmq_msg_recv(&msg_req, sd->socket, 0);
        if (zmq_rc != -1) {
            debug("Received a message");

            // create the response message
            response = Response_create();
            check_mem(response);

            request = Request_from_message(&msg_req);
            if (request == NULL) {
                log_warn("Could not unpack incoming message. Skipping");

                // send back an error
                response->requesttype = RHIZOFS__REQUEST_TYPE__UNKNOWN;
                response->errnotype = RHIZOFS__ERRNO__ERRNO_UNSERIALIZABLE;
            }
            else {
                int op_rc = 0;

                // ensure errno is reset to zero
                errno = 0;

                switch(request->requesttype) {
                    case RHIZOFS__REQUEST_TYPE__PING:
                        op_rc = ServeDir_op_ping(response);
                        break;

#define CASE_OP(CNAME, FNAME) \
                    case RHIZOFS__REQUEST_TYPE__ ## CNAME: \
                        op_rc = ServeDir_op_ ## FNAME (sd, request, response); \
                        break;

                    CASE_OP(READDIR, readdir)
                    CASE_OP(RMDIR, rmdir)
                    CASE_OP(UNLINK, unlink)
                    CASE_OP(ACCESS, access)
                    CASE_OP(RENAME, rename)
                    CASE_OP(MKDIR, mkdir)
                    CASE_OP(GETATTR, getattr)
                    CASE_OP(OPEN, open)
                    CASE_OP(READ, read)
                    CASE_OP(WRITE, write)
                    CASE_OP(CREATE, create)
                    CASE_OP(TRUNCATE, truncate)
                    CASE_OP(CHMOD, chmod)
                    CASE_OP(UTIMENS, utimens)
                    CASE_OP(LINK, link)
                    CASE_OP(SYMLINK, symlink)
                    CASE_OP(READLINK, readlink)
                    CASE_OP(MKNOD, mknod)
                    CASE_OP(STATFS, statfs)
#undef CASE_OP
                    default:
                        // dont know what to do with that request
                        op_rc = ServeDir_op_invalid(response);
                }

                if (op_rc != 0) {
                    /* Some early checks fail without setting a wire errno.
                     * Preserve specific errors, but never report success. */
                    if (response->errnotype == RHIZOFS__ERRNO__ERRNO_NONE) {
                        Response_set_errno(response, EIO);
                    }
                    log_warn("calling action failed");
                }
                Request_from_message_destroy(request);
            }

            zmq_msg_close(&msg_req);

            // serialize the reply
            check((Response_pack(response, &msg_rep) == true), "Could not pack message");

            //  Send reply back to client
            check((zmq_msg_send(&msg_rep, sd->socket, 0) != -1), "Could not send message");
            zmq_msg_close(&msg_rep);

            Response_destroy(response); response = NULL;
        }
        else {
            if (errno == ETERM) {
                debug("the context has been terminated leaving the servedir loop");
                zmq_msg_close (&msg_req);
                term_loop = 1;
            }
        }
    }

    debug("Exiting ServeDir_Serve");
    return true;

error:
    zmq_msg_close(&msg_req);
    zmq_msg_close(&msg_rep);
    if (response != NULL) Response_destroy(response);
    return false;
}


/* Return a descriptor for the directory containing reqpath and an allocated
 * final component.  Each intermediate component is opened relative to the
 * already verified directory descriptor with O_NOFOLLOW.  Consequently a
 * rename or symlink swap after this function returns cannot redirect an *at
 * operation outside the served directory. */
static int
ServeDir_parent(const ServeDir * sd, Rhizofs__Response * response,
        const char * reqpath, int * parentfd, char ** leaf)
{
    char * pathcopy = NULL;
    char * component = NULL;
    char * saveptr = NULL;
    int currentfd = -1;
    int returned_err = EINVAL;

    if (sd == NULL || reqpath == NULL || parentfd == NULL || leaf == NULL) {
        Response_set_errno(response, EINVAL);
        return -1;
    }

    *parentfd = -1;
    *leaf = NULL;
    returned_err = EACCES;
    if (path_has_parent_reference(reqpath)) goto error;
    currentfd = dup(sd->directory_fd);
    if (currentfd == -1) {
        returned_err = errno;
        goto error;
    }

    returned_err = ENOMEM;
    pathcopy = strdup(reqpath);
    if (pathcopy == NULL) goto error;
    component = strtok_r(pathcopy, "/", &saveptr);
    if (component == NULL) {
        *leaf = strdup(".");
        if (*leaf == NULL) goto error;
        *parentfd = currentfd;
        free(pathcopy);
        return 0;
    }

    while (component != NULL) {
        char * next = strtok_r(NULL, "/", &saveptr);
        if (next == NULL) {
            *leaf = strdup(component);
            if (*leaf == NULL) goto error;
            *parentfd = currentfd;
            free(pathcopy);
            return 0;
        }

        if (strcmp(component, ".") != 0) {
            int nextfd = openat(currentfd, component,
                    DIRECTORY_ACCESS | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (nextfd == -1) {
                /* O_NOFOLLOW on a symlink directory may report ENOTDIR
                 * or ELOOP. Preserve the resolver's access-denied policy
                 * while keeping allocation failures distinguishable. */
                returned_err = errno == ENOMEM ? ENOMEM : EACCES;
                goto error;
            }
            close(currentfd);
            currentfd = nextfd;
        }
        component = next;
    }

error:
    /* Preserve master's explicit resolver errors: a refused path must
     * never result in a successful wire response or hide ENOMEM. */
    Response_set_errno(response, returned_err);
    free(pathcopy);
    free(*leaf);
    *leaf = NULL;
    if (currentfd != -1) close(currentfd);
    return -1;
}

// ########## filesystem operations ############################

/**
 * check if the request has a optional (in the proto defintion)
 * parameter with given name, error otherwise
 */
#define REQ_HAS_OPTIONAL(REQ, RESP, PNAME) \
    if (!REQ->has_ ## PNAME) { \
        log_err(STRINGIFY(REQ) " is missing the " STRINGIFY(PNAME) " parameter"); \
        RESP->errnotype = RHIZOFS__ERRNO__ERRNO_INVALID_REQUEST; \
        return -1; \
    }

/**
 * check if the request has a non-NULL pointer with the
 * given name, error otherwise
 */
#define REQ_HAS_OPTIONAL_PTR(REQ, RESP, PTRNAME) \
    if (!REQ->PTRNAME) { \
        log_err(STRINGIFY(REQ) " is missing the " STRINGIFY(PTRNAME) " pointer"); \
        RESP->errnotype = RHIZOFS__ERRNO__ERRNO_INVALID_REQUEST; \
        return -1; \
    }


static int
ServeDir_op_ping(Rhizofs__Response * response)
{
    debug("PING");
    response->requesttype = RHIZOFS__REQUEST_TYPE__PING;

    return 0; // always successful
}


static int
ServeDir_op_invalid(Rhizofs__Response * response)
{
    log_warn("INVALID REQUEST");

    // dont know what to do with that request
    response->requesttype = RHIZOFS__REQUEST_TYPE__INVALID;
    response->errnotype = RHIZOFS__ERRNO__ERRNO_INVALID_REQUEST;

    return 0; // always successful
}


static int
ServeDir_op_readdir(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    DIR *dir = NULL;
    char * leaf = NULL;
    int parentfd = -1;
    int openedfd = -1;
    struct dirent *de = NULL;
    size_t entry_count = 0;
    size_t entries_capacity = 0;
    struct stat sb;

    debug("READDIR");
    response->requesttype = RHIZOFS__REQUEST_TYPE__READDIR;
    response->n_directory_entries = 0;

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble directory path.");
    openedfd = openat(parentfd, leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (openedfd != -1) dir = fdopendir(openedfd);
    if (dir == NULL) {
        Response_set_errno(response, errno);
        if (openedfd != -1) close(openedfd);
        free(leaf);
        close(parentfd);
        return 0;
    }
    openedfd = -1; /* owned by dir */

    // count the entries in the directory
    while (readdir(dir) != NULL) {
        ++entry_count;
    }

    /* the count above is only an estimate: the directory may well have
     * grown by the time the second pass below runs - another worker
     * thread serving a CREATE/WRITE request for this directory, or any
     * other process writing to the shared directory, is enough. the
     * second pass used to write past the end of this array whenever
     * that happened, so grow the array instead of trusting the count.
     * always keep room for at least one entry, as calloc(0, ..) may
     * return NULL. */
    entries_capacity = entry_count + 1;
    response->directory_entries = (Rhizofs__Attrs**)calloc(entries_capacity, sizeof(Rhizofs__Attrs *));
    check_mem_response(response->directory_entries);

    rewinddir(dir);
    while ((de = readdir(dir)) != NULL) {
        debug("found directory entry %s",  de->d_name);

        if (response->n_directory_entries == entries_capacity) {
            size_t new_capacity = entries_capacity * 2;
            Rhizofs__Attrs ** grown = realloc(response->directory_entries,
                    new_capacity * sizeof(Rhizofs__Attrs *));

            /* on failure the old array is still valid and is freed by
             * the error handler below */
            check_mem_response(grown);

            memset(grown + entries_capacity, 0,
                    (new_capacity - entries_capacity) * sizeof(Rhizofs__Attrs *));
            response->directory_entries = grown;
            entries_capacity = new_capacity;
        }

        if (fstatat(dirfd(dir), de->d_name, &sb, AT_SYMLINK_NOFOLLOW) == 0)  {
            response->directory_entries[response->n_directory_entries] = Attrs_create(&sb, de->d_name);
            check((response->directory_entries[response->n_directory_entries] != NULL),
                        "could not create attrs from stat");
            ++response->n_directory_entries;
        }
        else {
            Response_set_errno(response, errno);
            log_warn("Could not stat directory entry %s", de->d_name);
        }
    }

    closedir(dir);
    free(leaf);
    close(parentfd);
    return 0;

error:
    if (response->n_directory_entries != 0) {
        unsigned int i = 0;
        for (i=0; i<response->n_directory_entries; i++) {
            Attrs_destroy(response->directory_entries[i]);
        }
    }
    free(response->directory_entries);

    if (dir != NULL) {
        closedir(dir);
    }
    else if (openedfd != -1) close(openedfd);
    free(leaf);
    if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_rmdir(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;

    debug("RMDIR");
    response->requesttype = RHIZOFS__REQUEST_TYPE__RMDIR;

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble directory path.");
    if (unlinkat(parentfd, leaf, AT_REMOVEDIR) == -1) {
        Response_set_errno(response, errno);
    }

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_unlink(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;

    debug("UNLINK");
    response->requesttype = RHIZOFS__REQUEST_TYPE__UNLINK;

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble file path.");
    if (unlinkat(parentfd, leaf, 0) == -1) {
        Response_set_errno(response, errno);
    }

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_access(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    mode_t localmode;

    debug("ACCESS");
    response->requesttype = RHIZOFS__REQUEST_TYPE__ACCESS;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);

    bool success = false;
    localmode = (mode_t)Permissions_to_bitmask(request->permissions, &success);
    check(success, "Could not create bitmask from access permissions");

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
    if (faccessat(parentfd, leaf, localmode, AT_SYMLINK_NOFOLLOW) == -1) {
        Response_set_errno(response, errno);
    }

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_rename(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf_from = NULL;
    char * leaf_to = NULL;
    int parent_from = -1;
    int parent_to = -1;

    debug("RENAME");
    response->requesttype = RHIZOFS__REQUEST_TYPE__RENAME;

    REQ_HAS_OPTIONAL_PTR(request, response, path_to);

    check_debug((ServeDir_parent(sd, response, request->path_to, &parent_to, &leaf_to) == 0),
            "Could not securely assemble destination path.");
    check_debug((ServeDir_parent(sd, response, request->path, &parent_from, &leaf_from) == 0),
            "Could not securely assemble source path.");
    if (renameat(parent_from, leaf_from, parent_to, leaf_to) == -1) {
        Response_set_errno(response, errno);
    }

    free(leaf_to); free(leaf_from); close(parent_to); close(parent_from);
    return 0;

error:
    free(leaf_to); free(leaf_from);
    if (parent_to != -1) close(parent_to);
    if (parent_from != -1) close(parent_from);
    return -1;
}


static int
ServeDir_op_link(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf_from = NULL;
    char * leaf_to = NULL;
    int parent_from = -1;
    int parent_to = -1;

    debug("LINK");
    response->requesttype = RHIZOFS__REQUEST_TYPE__LINK;

    REQ_HAS_OPTIONAL_PTR(request, response, path_to);

    check_debug((ServeDir_parent(sd, response, request->path_to, &parent_to, &leaf_to) == 0),
            "Could not securely assemble destination path.");
    check_debug((ServeDir_parent(sd, response, request->path, &parent_from, &leaf_from) == 0),
            "Could not securely assemble source path.");
    if (linkat(parent_from, leaf_from, parent_to, leaf_to, 0) == -1) {
        Response_set_errno(response, errno);
    }

    free(leaf_to); free(leaf_from); close(parent_to); close(parent_from);
    return 0;

error:
    free(leaf_to); free(leaf_from);
    if (parent_to != -1) close(parent_to);
    if (parent_from != -1) close(parent_from);
    return -1;
}


static int
ServeDir_op_symlink(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char *leaf = NULL;
    int parentfd = -1;

    debug("SYMLINK");
    response->requesttype = RHIZOFS__REQUEST_TYPE__SYMLINK;

    REQ_HAS_OPTIONAL_PTR(request, response, path_to);

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");

    if (symlinkat(request->path_to, parentfd, leaf) == -1) {
        Response_set_errno(response, errno);
    }

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_readlink(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char *leaf = NULL;
    int parentfd = -1;
    char link_target[PATH_MAX];

    debug("READLINK");
    response->requesttype = RHIZOFS__REQUEST_TYPE__READLINK;

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");

    /* readlink does not null terminate. (!!!) Use size of buffer minus 1 to have room
     * for termininating NUL */
    ssize_t target_len = readlinkat(parentfd, leaf, link_target, sizeof(link_target) - 1);
    if (target_len == -1) {
        Response_set_errno(response, errno);
    }
    else {
        link_target[target_len] = '\0';
        response->link_target = strdup(link_target);
        check((response->link_target != NULL), "Could not allocate link_target");
    }

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf);
    if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_mkdir(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    mode_t localmode;

    debug("MKDIR");
    response->requesttype = RHIZOFS__REQUEST_TYPE__MKDIR;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);

    bool success = false;
    localmode = (mode_t)Permissions_to_bitmask(request->permissions, &success);
    check(success, "Could not create bitmask from access permissions");

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
    if (mkdirat(parentfd, leaf, localmode) == -1) {
        Response_set_errno(response, errno);
    }

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_getattr(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    struct stat sb;

    debug("GETATTR");
    response->requesttype = RHIZOFS__REQUEST_TYPE__GETATTR;

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");

    if (fstatat(parentfd, leaf, &sb, AT_SYMLINK_NOFOLLOW) == 0)  {
        response->attrs = Attrs_create(&sb, NULL);
        check((response->attrs != NULL), "could not create attrs from stat");
    }
    else {
        Response_set_errno(response, errno);
    }

    free(leaf); close(parentfd);
    return 0;

error:

    Attrs_destroy(response->attrs);
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_open(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    int openflags = 0;;
    bool success;
    int fd;

    debug("OPEN");
    response->requesttype = RHIZOFS__REQUEST_TYPE__OPEN;

    REQ_HAS_OPTIONAL_PTR(request, response, openflags);

    openflags = OpenFlags_to_bitmask(request->openflags, &success);
    check((success == true), "could not convert openflags to bitmask");

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
    fd = openat(parentfd, leaf, openflags | O_NOFOLLOW,
            default_file_creation_permissions);
    if (fd < 0) {
        Response_set_errno(response, errno);
        errno = 0;
        goto error;
    }

    close(fd);
    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_read(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    int fd = -1;
    ssize_t bytes_read;
    uint8_t * databuf = NULL;

    debug("READ");
    response->requesttype = RHIZOFS__REQUEST_TYPE__READ;

    REQ_HAS_OPTIONAL(request, response, size);
    REQ_HAS_OPTIONAL(request, response, offset);

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
    fd = openat(parentfd, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd != -1) {

        check((request->size >= 0), "requested size is negative: %d", (int)request->size);

        size_t read_size = (size_t)request->size;
        if (read_size > MAX_READ_SIZE) {
            debug("serving a read of %lld bytes up to the limit of %lld bytes",
                    (long long)read_size, (long long)MAX_READ_SIZE);
            read_size = MAX_READ_SIZE;
        }

        /* allocate using the same (non-truncated) size that is passed to
         * read()/pread() below - request->size is a 64bit value coming
         * from the client, truncating it here for the allocation while
         * using the full value for the read would allow a heap buffer
         * overflow */
        databuf = calloc(read_size, sizeof(uint8_t));
        check_mem(databuf);

        if (request->offset == 0) {
            /* use read to enable reading from non-seekable files */
            bytes_read = read(fd, databuf, read_size);
        }
        else {
            bytes_read = pread(fd, databuf, read_size, (off_t)request->offset);
        }
        /*
        check((request->size == bytes_read),
                "bytes_read (%d) and request->size (%d)differ",
                (int)bytes_read, (int)request->size);
        */

        if (bytes_read != -1) {
            check((Response_set_data(response, databuf, (size_t)bytes_read) == true),
                    "could not set response data");
        }
        else {
            Response_set_errno(response, errno);
            free(databuf);
            databuf = NULL;
        }
        close(fd);
    }
    else {
        Response_set_errno(response, errno);
    }

    free(databuf);
    free(leaf); close(parentfd);
    return 0;

error:
    free(databuf);
    if (fd >= 0) close(fd);
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}

static int
ServeDir_op_write(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    int fd = -1;
    uint8_t * data = NULL;

    debug("WRITE");
    response->requesttype = RHIZOFS__REQUEST_TYPE__WRITE;

    REQ_HAS_OPTIONAL(request, response, size);
    REQ_HAS_OPTIONAL(request, response, offset);

    if (Request_has_data(request) == -1) {
        log_err("the request does not contain data");
        response->errnotype = RHIZOFS__ERRNO__ERRNO_INVALID_REQUEST;
        return -1;
    }

    if ((request->size < 0) || ((uint64_t)request->size > MAX_WRITE_SIZE) ||
            (request->datablock->size < 0) ||
            ((uint64_t)request->datablock->size > MAX_WRITE_SIZE) ||
            (request->datablock->size != request->size) ||
            (request->datablock->data.len > MAX_WRITE_SIZE)) {
        log_warn("rejecting write with invalid or oversized data block");
        response->errnotype = RHIZOFS__ERRNO__ERRNO_INVALID_REQUEST;
        return 0;
    }

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
    fd = openat(parentfd, leaf, O_CREAT | O_WRONLY | O_NOFOLLOW | O_CLOEXEC,
            default_file_creation_permissions);
    if (fd != -1) {
        int bytes_in_block = DataBlock_get_data(request->datablock, &data);
        check((bytes_in_block != -1), "Could not extract data from datablock")
        check((data != NULL), "Extract data from datablock is NULL")
        check((bytes_in_block == request->size), "the number of bytes in the datablock "
                    "does not match the write requests size");

        ssize_t bytes_written = pwrite(fd, data, (size_t)request->size, (off_t)request->offset);
        if (bytes_written == -1) {
            Response_set_errno(response, errno);
            debug("Could not write %ld bytes", (int64_t)request->size);
            errno = 0;
        }

        response->has_size = 1;
        response->size = (int)bytes_written;

        struct timeval now;
        struct timeval times[2];

        gettimeofday(&now, NULL);
        times[0].tv_sec  = now.tv_sec;
        times[0].tv_usec = now.tv_usec;
        times[1].tv_sec  = now.tv_sec;
        times[1].tv_usec = now.tv_usec;

        if (futimes(fd, times) != 0) {
            log_warn("could not update timestamps after write");
        }
        check((close(fd) != -1), "Could not close file opened for writing.");
        fd = -1;
    }
    else {
        Response_set_errno(response, errno);
        errno = 0;
    }


    free(data);
    free(leaf); close(parentfd);
    return 0;

error:

    if (fd >= 0) close(fd);
    free(data);
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_create(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    bool success;
    mode_t create_mode = 0;
    int fd;
    /* Older clients omitted openflags and expected creat() semantics. */
    int openflags = O_WRONLY | O_CREAT | O_TRUNC;

    debug("CREATE");
    response->requesttype = RHIZOFS__REQUEST_TYPE__CREATE;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);

    create_mode = Permissions_to_bitmask(request->permissions, &success);
    check((success == true), "could not convert permissions to bitmask");

    if (request->openflags != NULL) {
        openflags = OpenFlags_to_bitmask(request->openflags, &success) | O_CREAT;
        check(success, "could not convert create openflags to bitmask");
    }

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");

    // always add S_IWUSR as creat allows creating files
    // and opening them without write permissions for the owner
    // set.
    // As this fuse filesystem will open the newly created file
    // again when on the next write call this approach will not work here.
    // So we will simply add write permissions for the owner here.
    create_mode |= S_IWUSR;

    fd = openat(parentfd, leaf, openflags | O_NOFOLLOW | O_CLOEXEC,
            create_mode);
    if (fd == -1) {
        Response_set_errno(response, errno);
        errno = 0;
        goto error;
    }

    close(fd);

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_truncate(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    int fd = -1;

    debug("TRUNCATE");
    response->requesttype = RHIZOFS__REQUEST_TYPE__TRUNCATE;

    REQ_HAS_OPTIONAL(request, response, offset);

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
    fd = openat(parentfd, leaf, O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1 || ftruncate(fd, request->offset) != 0) {
        Response_set_errno(response, errno);
    }

    if (fd != -1) close(fd);
    free(leaf); close(parentfd);
    return 0;

error:
    if (fd != -1) close(fd);
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


/* Older glibc rejects AT_SYMLINK_NOFOLLOW, and its later emulation needs
 * /proc. Prefer an inode-anchored fallback whenever /proc is available. */
static int
ServeDir_chmodat(int parentfd, const char * leaf, mode_t mode)
{
    int rc = fchmodat(parentfd, leaf, mode, AT_SYMLINK_NOFOLLOW);
    if (rc == 0 || (errno != ENOTSUP && errno != EOPNOTSUPP)) return rc;

#if defined(__linux__) && defined(O_PATH)
    int fd = openat(parentfd, leaf, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) return -1;

    struct stat sb;
    if (fstat(fd, &sb) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    if (S_ISLNK(sb.st_mode)) {
        close(fd);
        errno = EOPNOTSUPP;
        return -1;
    }

    char procpath[64];
    snprintf(procpath, sizeof(procpath), "/proc/self/fd/%d", fd);
    rc = chmod(procpath, mode);
    if (rc != 0 && (errno == ENOENT || errno == ENOTDIR)) {
        /* Without /proc, old kernels cannot chmod an O_PATH fd. Never
         * retry by name: a swapped leaf could now point outside the share. */
        errno = EOPNOTSUPP;
    }
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
#endif
    return rc;
}

static int
ServeDir_op_chmod(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    mode_t localmode = 0;

    debug("CHMOD");
    response->requesttype = RHIZOFS__REQUEST_TYPE__CHMOD;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);

    bool success = false;
    localmode = (mode_t)Permissions_to_bitmask(request->permissions, &success);
    check(success, "Could not create bitmask from chmod permissions");

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
    if (ServeDir_chmodat(parentfd, leaf, localmode) != 0) {
        Response_set_errno(response, errno);
    }

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_utimens(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    struct timespec times[2];

    debug("UTIMENS");
    response->requesttype = RHIZOFS__REQUEST_TYPE__UTIMENS;

    REQ_HAS_OPTIONAL_PTR(request, response, timestamps);

    times[0].tv_sec  = request->timestamps->access_sec;
    times[0].tv_nsec = (long)request->timestamps->access_usec * 1000;
    times[1].tv_sec  = request->timestamps->modify_sec;
    times[1].tv_nsec = (long)request->timestamps->modify_usec * 1000;

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
    if (utimensat(parentfd, leaf, times, AT_SYMLINK_NOFOLLOW) != 0) {
        Response_set_errno(response, errno);
    }

    free(leaf); close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}


static int
ServeDir_op_mknod(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    mode_t mode;

    debug("MKNOD");
    response->requesttype = RHIZOFS__REQUEST_TYPE__MKNOD;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);
    REQ_HAS_OPTIONAL(request, response, filetype);

    bool success = false;
    mode = (mode_t)Permissions_to_bitmask(request->permissions, &success);
    check(success, "Could not create bitmask from mknod permissions");

    mode |= FileType_to_local(request->filetype);

    if (S_ISREG(mode)) {

        check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
                "Could not securely assemble path.");

        if (mknodat(parentfd, leaf, mode, 0) != 0) {
            Response_set_errno(response, errno);
        }
    }
    else {
        Response_set_errno(response, EPERM);
        debug("No permission to call mknod with other filetypes than regular files");
    }

    free(leaf); if (parentfd != -1) close(parentfd);
    return 0;

error:
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}

static int
ServeDir_op_statfs(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * leaf = NULL;
    int parentfd = -1;
    int fd = -1;
    struct statvfs stfs;

    debug("STATFS");
    response->requesttype = RHIZOFS__REQUEST_TYPE__STATFS;

    check_debug((ServeDir_parent(sd, response, request->path, &parentfd, &leaf) == 0),
            "Could not securely assemble path.");
#if defined(O_PATH)
    fd = openat(parentfd, leaf, O_PATH | O_NOFOLLOW | O_CLOEXEC);
#else
    /* Filesystem statistics do not require opening the file for reading.
     * Use its anchored parent on platforms without O_PATH. */
    struct stat sb;
    if (fstatat(parentfd, leaf, &sb, AT_SYMLINK_NOFOLLOW) == 0) {
        fd = dup(parentfd);
    }
#endif

    if (fd != -1 && fstatvfs(fd, &stfs) == 0)  {
        response->statfs = StatFs_create(&stfs);
        check((response->statfs != NULL), "could not create attrs from stat");
    }
    else {
        Response_set_errno(response, errno);
    }

    if (fd != -1) close(fd);
    free(leaf); close(parentfd);
    return 0;

error:

    StatFs_destroy(response->statfs);
    if (fd != -1) close(fd);
    free(leaf); if (parentfd != -1) close(parentfd);
    return -1;
}

