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

    log_info("Serving directory <%s> on <%s>", sd->directory, sd->socket_name);

    while (1) {
        check((zmq_msg_init(&msg_req) == 0), "Could not initialize request message");        

        zmq_recv (sd->socket, &msg_req, 0);
        debug("Received a message\n");    

        // create the response message
        Rhizofs__Response response = RHIZOFS__RESPONSE__INIT;
        Rhizofs__Version version = RHIZOFS__VERSION__INIT;
        version.major = RHI_VERSION_MAJOR; 
        version.minor = RHI_VERSION_MINOR; 
        response.version = &version;

        request = rhizofs__request__unpack(NULL, 
            zmq_msg_size(&msg_req),
            zmq_msg_data(&msg_req)); 

        if (request == NULL) {
            log_warn("Could not unpack incoming message. Skipping");

            // send back an error
            response.requesttype = RHIZOFS__REQUEST_TYPE__UNKNOWN;
            response.error = RHIZOFS__ERROR_TYPE__UNSERIALIZABLE_REQUEST;
        }
        else {
            response.requesttype = request->requesttype;

            switch(request->requesttype) {

                case RHIZOFS__REQUEST_TYPE__PING:
                    debug("Request: PING");
                    response.requesttype = RHIZOFS__REQUEST_TYPE__PING;
                    break;
            
                default:
                    // dont know what to do with that request
                    log_warn("recieved an invalid request");
                    response.error = RHIZOFS__ERROR_TYPE__INVALID_REQUEST; 
            }

            rhizofs__request__free_unpacked(request, NULL);
        }

        zmq_msg_close (&msg_req);

        // serialize the reply
        size_t len = (size_t)rhizofs__response__get_packed_size(&response);

        zmq_msg_init_size(&msg_rep, len); // CHECK
        rhizofs__response__pack(&response, zmq_msg_data(&msg_rep));

        
        //  Send reply back to client   
        zmq_send (sd->socket, &msg_rep, 0);
        zmq_msg_close (&msg_rep);
    }

    return 0;

error:

    zmq_msg_close (&msg_req);
    zmq_msg_close (&msg_rep);

    return -1;
}
