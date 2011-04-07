#include "servedir.h"


ServeDir *
ServeDir_create(void *context, char *socket_name, char *directory)
{
    ServeDir * sd = calloc(sizeof(ServeDir), 1);
    check_mem(sd);
    sd->socket = NULL;

    // validate the directory
    struct stat sr;
    check((stat((const char*)directory, &sr) == 0), "could not stat %s", directory);
    check(S_ISDIR(sr.st_mode), "%s is not a directory.", directory);
    sd->directory = directory;

    sd->socket = zmq_socket(context, ZMQ_REP);
    sd->socket_name = socket_name;
    check((sd->socket != NULL), "Could not create zmq socket");
    check((zmq_bind(sd->socket, socket_name) == 0), "could not bind to socket %s", socket_name);

    return sd;

error:
    if (sd->socket) {
        zmq_term(sd->socket);
    }

    free(sd);

    return NULL;
}


void
ServeDir_destroy(ServeDir * sd)
{
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

    log_info("Serving directory <%s> on <%s>", sd->directory, sd->socket_name);

    while (1) {
        check((zmq_msg_init(&msg_req) == 0), "Could not initialize request message");        

        check((zmq_recv (sd->socket, &msg_req, 0) == 0), "Could not recv message");
        debug("Received a message");    

        // create the response message
        response = Response_create();
        check_mem(response);

        request = rhizofs__request__unpack(NULL, 
            zmq_msg_size(&msg_req),
            zmq_msg_data(&msg_req)); 

        if (request == NULL) {
            log_warn("Could not unpack incoming message. Skipping");

            // send back an error
            response->requesttype = RHIZOFS__REQUEST_TYPE__UNKNOWN;
            response->errortype = RHIZOFS__ERROR_TYPE__UNSERIALIZABLE_REQUEST;
        }
        else {

            switch(request->requesttype) {

                case RHIZOFS__REQUEST_TYPE__PING:
                    debug("PING");
                    response->requesttype = RHIZOFS__REQUEST_TYPE__PING;
                    break;

                case RHIZOFS__REQUEST_TYPE__READDIR:
                    debug("READDIR: path: %s", request->path);
                    response->requesttype = RHIZOFS__REQUEST_TYPE__READDIR;

                    if (request->path == NULL) {
                        response->errortype = RHIZOFS__ERROR_TYPE__INVALID_REQUEST; 
                        debug("READDIR invalid (%d)", response->errortype);
                    }
                    break;
            
                default:
                    // dont know what to do with that request
                    response->requesttype = RHIZOFS__REQUEST_TYPE__INVALID; 
                    response->errortype = RHIZOFS__ERROR_TYPE__INVALID_REQUEST; 
                    log_warn("recieved an invalid request");
            }

            rhizofs__request__free_unpacked(request, NULL);
        }

        zmq_msg_close (&msg_req);

        // serialize the reply
        size_t len = (size_t)rhizofs__response__get_packed_size(response);
        debug("Response will be %d bytes long", len);

        check((zmq_msg_init_size(&msg_rep, len) == 0), "Could not initialize message");
        check((rhizofs__response__pack(response, zmq_msg_data(&msg_rep)) == len), "Could not pack message");
        
        //  Send reply back to client   
        check((zmq_send(sd->socket, &msg_rep, 0) == 0), "Could not send message");
        zmq_msg_close (&msg_rep);

        Response_destroy(response);
    }

    return 0;

error:

    zmq_msg_close (&msg_req);
    zmq_msg_close (&msg_rep);

    Response_destroy(response);

    return -1;
}
