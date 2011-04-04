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
    log_info("Serving directory <%s> on <%s>", sd->directory, sd->socket_name);


    while (1) {
        zmq_msg_t request;
        zmq_msg_t reply;

        zmq_msg_init (&request);

        zmq_recv (sd->socket, &request, 0);
        debug("Received a message\n");
        zmq_msg_close (&request);

        //  Do some 'work'
        /*
        respone = new response()
        Serve_handle(request, &response)
         */

        //  Send reply back to client
        zmq_msg_init_data (&reply, "World", 5, NULL, NULL);
        zmq_send (sd->socket, &reply, 0);
        zmq_msg_close (&reply);
    }

    return 0;

error:

    return -1;
}


