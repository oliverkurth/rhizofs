#include "dbg.h"
#include "servedir.h"

// check for memory and set response error on failure
#define check_mem_response(A) if(!(A)) { log_err("Out of memory."); response->errortype = RHIZOFS__ERROR_TYPE__NO_MEMORY ; errno=0; ; goto error; }



ServeDir *
ServeDir_create(void *context, char *socket_name, char *directory)
{
    ServeDir * sd = calloc(sizeof(ServeDir), 1);
    check_mem(sd);
    sd->socket = NULL;

    // get the absolute path to the directory
    sd->directory = calloc(sizeof(char), PATH_MAX);
    check_mem(sd->directory);
    check((realpath(directory, sd->directory) != NULL), "Could not resolve directory path");

    // validate the directory
    struct stat sr;
    check((stat((const char*)directory, &sr) == 0), "could not stat %s", directory);
    check(S_ISDIR(sr.st_mode), "%s is not a directory.", directory);

    sd->socket = zmq_socket(context, ZMQ_REP);
    sd->socket_name = socket_name;
    check((sd->socket != NULL), "Could not create zmq socket");
    check((zmq_bind(sd->socket, socket_name) == 0), "could not bind to socket %s", socket_name);

    return sd;

error:

    if (sd->directory != NULL) {
        free(sd->directory); // free the memory allocated by realpath
    }

    if (sd->socket) {
        zmq_term(sd->socket);
    }

    free(sd);

    return NULL;
}


void
ServeDir_destroy(ServeDir * sd)
{

    if (sd->directory != NULL) {
        free(sd->directory); // free the memory allocated by realpath
    }

    if (sd->socket != NULL) {
        zmq_close(sd->socket);
        sd->socket = NULL;
    }
    free(sd);
}


int
ServeDir_serve(ServeDir * sd)
{
    zmq_msg_t msg_req;
    zmq_msg_t msg_rep;
    Rhizofs__Request *request;
    Rhizofs__Response *response = NULL;

    debug("Serving directory <%s> on <%s>", sd->directory, sd->socket_name);

    while (1) {
        check((zmq_msg_init(&msg_req) == 0), "Could not initialize request message");

        check((zmq_recv (sd->socket, &msg_req, 0) == 0), "Could not recv message");
        debug("Received a message");

        // create the response message
        response = Response_create();
        check_mem(response);

        request = Request_from_message(&msg_req);
        if (request == NULL) {
            log_warn("Could not unpack incoming message. Skipping");

            // send back an error
            response->requesttype = RHIZOFS__REQUEST_TYPE__UNKNOWN;
            response->errortype = RHIZOFS__ERROR_TYPE__UNSERIALIZABLE_REQUEST;
        }
        else {
            int action_rc = 0;

            switch(request->requesttype) {

                case RHIZOFS__REQUEST_TYPE__PING:
                    action_rc = ServeDir_action_ping(&response);
                    break;

                case RHIZOFS__REQUEST_TYPE__READDIR:
                    action_rc = ServeDir_action_readdir(sd, request, &response);
                    break;

                case RHIZOFS__REQUEST_TYPE__RMDIR:
                    action_rc = ServeDir_action_rmdir(sd, request, &response);
                    break;

                case RHIZOFS__REQUEST_TYPE__UNLINK:
                    action_rc = ServeDir_action_unlink(sd, request, &response);
                    break;

                default:
                    // dont know what to do with that request
                    //action_rc = action_invalid(sd, request, &response);
                    action_rc = ServeDir_action_invalid(&response);
            }

            if (action_rc != 0) {
                log_warn("calling action failed");
            }

            Request_from_message_destroy(request);
        }

        zmq_msg_close (&msg_req);

        // serialize the reply
        check((Response_pack(response, &msg_rep) == 0), "Could not pack message");

        //  Send reply back to client
        check((zmq_send(sd->socket, &msg_rep, 0) == 0), "Could not send message");
        zmq_msg_close (&msg_rep);

        Response_destroy(response);
    }

    return 0;

error:

    zmq_msg_close(&msg_req);
    zmq_msg_close(&msg_rep);

    Response_destroy(response);

    return -1;
}


int
ServeDir_fullpath(const ServeDir * sd, const Rhizofs__Request * request, char ** fullpath)
{
    check((request->path != NULL), "request path is null");
    check((path_join(sd->directory, request->path, fullpath)==0), "error processing path");
    check_debug((fullpath != NULL), "fullpath is null");

    return 0;
error:
    return -1;
}


int
ServeDir_action_ping(Rhizofs__Response **resp)
{
    Rhizofs__Response * response = (*resp);
    
    debug("PING");
    response->requesttype = RHIZOFS__REQUEST_TYPE__PING;

    return 0; // always successful
}


int
ServeDir_action_invalid(Rhizofs__Response **resp)
{
    Rhizofs__Response * response = (*resp);
    
    log_warn("INVALID REQUEST");

    // dont know what to do with that request
    response->requesttype = RHIZOFS__REQUEST_TYPE__INVALID;
    response->errortype = RHIZOFS__ERROR_TYPE__INVALID_REQUEST;

    return 0; // always successful
}


int
ServeDir_action_readdir(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response **resp)
{
    DIR *dir = NULL;
    char * dirpath = NULL;
    struct dirent *de;
    size_t entry_count = 0;
    Rhizofs__Response * response = (*resp);

    debug("READDIR");

    response->requesttype = RHIZOFS__REQUEST_TYPE__READDIR;
    response->n_directory_entries = 0;

    check_debug((ServeDir_fullpath(sd, request, &dirpath) == 0), "Could not assemble directory path.");
    debug("requested directory path: %s", dirpath);
    dir = opendir(dirpath);
    if (dir == NULL) {
        Response_set_errno(&response, errno);
        debug("Could not open directory %s", dirpath);

        free(dirpath);
        return 0;
    }

    // count the entries in the directory
    while (readdir(dir) != NULL) {
        ++entry_count;
    }

    response->directory_entries = (char**)calloc(sizeof(char *), entry_count);
    check_mem_response(response->directory_entries);

    rewinddir(dir);
    while ((de = readdir(dir)) != NULL) {
        debug("found directory entry %s",  de->d_name);

        response->directory_entries[response->n_directory_entries] = (char *)calloc(sizeof(char), (strlen(de->d_name)+1) );
        check_mem_response(response->directory_entries[response->n_directory_entries]);
        strcpy(response->directory_entries[response->n_directory_entries], de->d_name);

        ++response->n_directory_entries;
    }

    closedir(dir);
    free(dirpath);
    return 0;

error:
    if (response->n_directory_entries != 0) {
        unsigned int i = 0;
        for (i=0; i<response->n_directory_entries; i++) {
            free(response->directory_entries[i]);
        }
    }
    free(response->directory_entries);


    if (dir != NULL) {
        closedir(dir);
    }
    free(dirpath);
    return -1;
}


int
ServeDir_action_rmdir(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response **resp)
{
    char * path = NULL;
    Rhizofs__Response * response = (*resp);

    debug("RMDIR");

    response->requesttype = RHIZOFS__REQUEST_TYPE__RMDIR;

    check_debug((ServeDir_fullpath(sd, request, &path) == 0), "Could not assemble directory path.");
    debug("requested directory path: %s", path);
    if (rmdir(path) == -1) {
        Response_set_errno(&response, errno);
        debug("Could not remove directory %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}


int
ServeDir_action_unlink(const ServeDir * sd, Rhizofs__Request * request, Rhizofs__Response **resp)
{
    char * path = NULL;
    Rhizofs__Response * response = (*resp);

    debug("UNLINK");

    response->requesttype = RHIZOFS__REQUEST_TYPE__UNLINK;

    check_debug((ServeDir_fullpath(sd, request, &path) == 0), "Could not assemble file path.");
    debug("requested path: %s", path);
    if (unlink(path) == -1) {
        Response_set_errno(&response, errno);
        debug("Could not unlink %s", path);
    }

    free(path);
    return 0;

error:
    free(path);
    return -1;
}


