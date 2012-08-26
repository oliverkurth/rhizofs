#include "servedir.h"

#include <limits.h> /* for PATH_MAX */
#include <stdbool.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

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
 * default permissions for file creation. the same permission set is used
 * by the GNU coreutils touch command
 */
static const int default_file_creation_permissions = 
        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;


// prototypes
static int ServeDir_fullpath(const ServeDir * sd, const Rhizofs__Request * request, char ** fullpath);
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
#undef SERVEDIR_OP



ServeDir *
ServeDir_create(void *context, char *socket_name, char *directory)
{
    ServeDir * sd = NULL;
    sd = (ServeDir *)calloc(sizeof(ServeDir), 1);
    check_mem(sd);

    sd->socket = NULL;
    sd->directory = NULL;
    struct stat sr;

    /* get the absolute path to the directory */
    sd->directory = calloc(sizeof(char), PATH_MAX);
    check_mem(sd->directory);
    check((realpath(directory, sd->directory) != NULL),
            "Could not resolve directory path");

    /* validate the directory */
    check((stat((const char*)directory, &sr) == 0), "could not stat %s", directory);
    check(S_ISDIR(sr.st_mode), "%s is not a directory.", directory);

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

        zmq_rc = zmq_recv(sd->socket, &msg_req, 0);
        if (zmq_rc == 0) {

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
#undef CASE_OP
                    default:
                        // dont know what to do with that request
                        op_rc = ServeDir_op_invalid(response);
                }

                if (op_rc != 0) {
                    log_warn("calling action failed");
                }
                Request_from_message_destroy(request);
            }

            zmq_msg_close(&msg_req);

            // serialize the reply
            check((Response_pack(response, &msg_rep) == true), "Could not pack message");

            //  Send reply back to client
            check((zmq_send(sd->socket, &msg_rep, 0) == 0), "Could not send message");
            zmq_msg_close(&msg_rep);

            Response_destroy(response);
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
    Response_destroy(response);
    return false;
}


static int
ServeDir_fullpath(const ServeDir * sd, const Rhizofs__Request * request, char ** fullpath)
{
    check((request->path != NULL), "request path is null");
    check((path_join(sd->directory, request->path, fullpath)==0), "error processing path");
    check_debug((fullpath != NULL), "fullpath is null");

    return 0;
error:
    return -1;
}

// ########## fileststem operations ############################

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
    char * dirpath = NULL;
    struct dirent *de = NULL;
    size_t entry_count = 0;
    char * entry_fullpath = NULL;
    struct stat sb;

    debug("READDIR");
    response->requesttype = RHIZOFS__REQUEST_TYPE__READDIR;
    response->n_directory_entries = 0;

    check_debug((ServeDir_fullpath(sd, request, &dirpath) == 0),
            "Could not assemble directory path.");
    debug("requested directory path: %s", dirpath);
    dir = opendir(dirpath);
    if (dir == NULL) {
        Response_set_errno(response, errno);
        debug("Could not open directory %s", dirpath);

        free(dirpath);
        return 0;
    }

    // count the entries in the directory
    while (readdir(dir) != NULL) {
        ++entry_count;
    }

    response->directory_entries = (Rhizofs__Attrs**)calloc(sizeof(Rhizofs__Attrs *), entry_count);
    check_mem_response(response->directory_entries);

    rewinddir(dir);
    while ((de = readdir(dir)) != NULL) {
        debug("found directory entry %s",  de->d_name);

        check((path_join(dirpath, de->d_name, &entry_fullpath)==0),
            "error processing path for directory entry");

        if (stat(entry_fullpath, &sb) == 0)  {
            response->directory_entries[response->n_directory_entries] = Attrs_create(&sb, de->d_name);
            check((response->directory_entries[response->n_directory_entries] != NULL),
                        "could not create attrs from stat");
            ++response->n_directory_entries;
        }
        else {
            Response_set_errno(response, errno);
            log_warn("Could not stat %s", entry_fullpath);
        }
        free(entry_fullpath);
    }

    closedir(dir);
    free(dirpath);
    return 0;

error:
    if (response->n_directory_entries != 0) {
        unsigned int i = 0;
        for (i=0; i<response->n_directory_entries; i++) {
            Attrs_destroy(response->directory_entries[i]);
        }
    }
    free(response->directory_entries);
    free(entry_fullpath);

    if (dir != NULL) {
        closedir(dir);
    }
    free(dirpath);
    return -1;
}


static int
ServeDir_op_rmdir(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;

    debug("RMDIR");
    response->requesttype = RHIZOFS__REQUEST_TYPE__RMDIR;

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble directory path.");
    debug("requested directory path: %s", path);
    if (rmdir(path) == -1) {
        Response_set_errno(response, errno);
        debug("Could not remove directory %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}


static int
ServeDir_op_unlink(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;

    debug("UNLINK");
    response->requesttype = RHIZOFS__REQUEST_TYPE__UNLINK;

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble file path.");
    debug("requested path: %s", path);
    if (unlink(path) == -1) {
        Response_set_errno(response, errno);
        debug("Could not unlink %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}


static int
ServeDir_op_access(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
    mode_t localmode;

    debug("ACCESS");
    response->requesttype = RHIZOFS__REQUEST_TYPE__ACCESS;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);

    bool success = false;
    localmode = (mode_t)Permissions_to_bitmask(request->permissions, &success);
    check(success, "Could not create bitmask from access permissions");

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("requested path: %s; accesmode: %o", path, localmode);
    if (access(path, localmode) == -1) {
        Response_set_errno(response, errno);
        debug("Could not call access on %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}


static int
ServeDir_op_rename(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path_from = NULL;
    char * path_to = NULL;

    debug("RENAME");
    response->requesttype = RHIZOFS__REQUEST_TYPE__RENAME;

    REQ_HAS_OPTIONAL_PTR(request, response, path_to);

    check((path_join(sd->directory, request->path_to, &path_to)==0),
            "error processing path_to");
    check_debug((path_to != NULL), "path_to is null");


    check_debug((ServeDir_fullpath(sd, request, &path_from) == 0),
            "Could not assemble path.");
    debug("requested path: %s -> %s", path_from, path_to);
    if (rename(path_from, path_to) == -1) {
        Response_set_errno(response, errno);
        debug("Could not rename %s to %s", path_from, path_to);
    }

    free(path_to);
    free(path_from);
    return 0;

error:
    free(path_to);
    free(path_from);
    return -1;
}


static int
ServeDir_op_link(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path_from = NULL;
    char * path_to = NULL;

    debug("LINK");
    response->requesttype = RHIZOFS__REQUEST_TYPE__LINK;

    REQ_HAS_OPTIONAL_PTR(request, response, path_to);

    check((path_join(sd->directory, request->path_to, &path_to)==0),
            "error processing path_to");
    check_debug((path_to != NULL), "path_to is null");


    check_debug((ServeDir_fullpath(sd, request, &path_from) == 0),
            "Could not assemble path.");
    debug("requested path: %s -> %s", path_from, path_to);
    if (link(path_from, path_to) == -1) {
        Response_set_errno(response, errno);
        debug("Could not link %s to %s", path_from, path_to);
    }

    free(path_to);
    free(path_from);
    return 0;

error:
    free(path_to);
    free(path_from);
    return -1;
}


static int
ServeDir_op_mkdir(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
    mode_t localmode;

    debug("MKDIR");
    response->requesttype = RHIZOFS__REQUEST_TYPE__MKDIR;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);

    bool success = false;
    localmode = (mode_t)Permissions_to_bitmask(request->permissions, &success);
    check(success, "Could not create bitmask from access permissions");

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("mkdir requested path: %s, mode: %d", path, (int)localmode);
    if (mkdir(path, localmode) == -1) {
        Response_set_errno(response, errno);
        debug("Could not call mkdir on %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}



static int
ServeDir_op_getattr(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
    struct stat sb;

    debug("GETATTR");
    response->requesttype = RHIZOFS__REQUEST_TYPE__GETATTR;

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("requested path: %s", path);

    if (stat(path, &sb) == 0)  {
        response->attrs = Attrs_create(&sb, NULL);
        check((response->attrs != NULL), "could not create attrs from stat");
    }
    else {
        Response_set_errno(response, errno);
        debug("Could not stat %s", path);
    }

    free(path);
    return 0;

error:

    Attrs_destroy(response->attrs);
    free(path);
    return -1;
}


static int
ServeDir_op_open(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
    int openflags = 0;;
    bool success;
    int fd;

    debug("OPEN");
    response->requesttype = RHIZOFS__REQUEST_TYPE__OPEN;

    REQ_HAS_OPTIONAL_PTR(request, response, openflags);

    openflags = OpenFlags_to_bitmask(request->openflags, &success);
    check((success == true), "could not convert openflags to bitmask");

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("requested path: %s, openflags: %o", path, openflags);
    fd = open(path, openflags, default_file_creation_permissions);
    if (fd == -1) {
        Response_set_errno(response, errno);
        debug("Could not call open on %s: %s", path, strerror(errno));
        errno = 0;
    }

    close(fd);
    free(path);
    return 0;

error:
    free(path);
    return -1;
}


static int
ServeDir_op_read(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
    int fd = -1;
    ssize_t bytes_read;
    uint8_t * databuf = NULL;

    debug("READ");
    response->requesttype = RHIZOFS__REQUEST_TYPE__READ;

    REQ_HAS_OPTIONAL(request, response, size);
    REQ_HAS_OPTIONAL(request, response, offset);

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("requested path: %s", path);
    fd = open(path, O_RDONLY);
    if (fd != -1) {

        databuf = calloc(sizeof(uint8_t), (int)request->size);
        check_mem(databuf);

        if (request->offset == 0) {
            /* use read to enable reading from non-seekable files */
            bytes_read = read(fd, databuf, (size_t)request->size);
        }
        else {
            bytes_read = pread(fd, databuf, (size_t)request->size, (off_t)request->offset);
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
            debug("Could not read from on %s", path);
            free(databuf);
            databuf = NULL;
        }
        close(fd);
    }
    else {
        Response_set_errno(response, errno);
        debug("Could not call open on %s", path);
    }

    free(databuf);
    free(path);
    return 0;

error:
    free(databuf);
    if (fd == -1) {
        close(fd);
    }
    free(path);
    return -1;
}

static int
ServeDir_op_write(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
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

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("requested path: %s", path);
    fd = open(path, O_CREAT | O_WRONLY, default_file_creation_permissions );
    if (fd != -1) {
        int bytes_in_block = DataBlock_get_data(request->datablock, &data);
        check((bytes_in_block != -1), "Could not extract data from datablock")
        check((data != NULL), "Extract data from datablock is NULL")
        check((bytes_in_block == request->size), "the number of bytes in the datablock "
                    "does not match the write requests size");

        ssize_t bytes_written = pwrite(fd, data, (size_t)request->size, (off_t)request->offset);
        if (bytes_written == -1) {
            Response_set_errno(response, errno);
            debug("Could not write %d bytes to %s", (int)request->size, path);
            errno = 0;
        }

        response->has_size = 1;
        response->size = (int)bytes_written;

        check((close(fd) != -1), "Could not close file opened for writing.");
    }
    else {
        Response_set_errno(response, errno);
        debug("Could not call open on %s", path);
        errno = 0;
    }


    free(data);
    free(path);
    return 0;

error:

    if (fd != -1) {
        close(fd);
    }
    free(data);
    free(path);
    return -1;
}


static int
ServeDir_op_create(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
    bool success;
    mode_t create_mode = 0;
    int fd;

    debug("CREATE");
    response->requesttype = RHIZOFS__REQUEST_TYPE__CREATE;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);

    create_mode = Permissions_to_bitmask(request->permissions, &success);
    check((success == true), "could not convert permissions to bitmask");

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");

    // always add S_IWUSR as creat allows creating files
    // and opening them without write permissions for the owner
    // set.
    // As this fuse filesystem will open the newly created file
    // again when on the next write call this approach will not work here.
    // So we will simply add write permissions for the owner here.
    create_mode |= S_IWUSR;

    debug("requested path: %s, create_mode: %o", path, create_mode);
    fd = creat(path, create_mode);
    if (fd == -1) {
        Response_set_errno(response, errno);
        debug("Could not call creat on %s: %s", path, strerror(errno));
        errno = 0;
    }

    close(fd);

    free(path);
    return 0;

error:
    free(path);
    return -1;
}


static int
ServeDir_op_truncate(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;

    debug("TRUNCATE");
    response->requesttype = RHIZOFS__REQUEST_TYPE__TRUNCATE;

    REQ_HAS_OPTIONAL(request, response, offset);

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("requested path: %s", path);
    if (truncate(path, request->offset) != 0) {
        Response_set_errno(response, errno);
        debug("Could not call truncate on %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}


static int
ServeDir_op_chmod(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
    mode_t localmode = 0;

    debug("CHMOD");
    response->requesttype = RHIZOFS__REQUEST_TYPE__CHMOD;

    REQ_HAS_OPTIONAL_PTR(request, response, permissions);

    bool success = false;
    localmode = (mode_t)Permissions_to_bitmask(request->permissions, &success);
    check(success, "Could not create bitmask from chmod permissions");

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("requested path: %s", path);
    if (chmod(path, localmode) != 0) {
        Response_set_errno(response, errno);
        debug("Could not call chmod on %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}


static int
ServeDir_op_utimens(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response *response)
{
    char * path = NULL;
    struct timeval times [2];

    debug("UTIMENS");
    response->requesttype = RHIZOFS__REQUEST_TYPE__UTIMENS;

    REQ_HAS_OPTIONAL_PTR(request, response, timestamps);

    times[0].tv_usec = 0;
    times[0].tv_sec  = request->timestamps->access;
    times[1].tv_usec = 0;
    times[1].tv_sec  = request->timestamps->modification;

    check_debug((ServeDir_fullpath(sd, request, &path) == 0),
            "Could not assemble path.");
    debug("requested path: %s", path);
    if (utimes(path, times) != 0) {
        Response_set_errno(response, errno);
        debug("Could not call utimes on %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}
