#include "serve.h"

int
Serve_init()
{
    if (context == NULL) {
        context = zmq_init(1);
        check((context != NULL), "Could not create Zmq context");
    }

    return 0;

error:
    return -1;
}


void 
Serve_destroy()
{
    if (context != NULL) {
        zmq_term(context);      
        context = NULL;
    }   
}


int
Serve_directory(const char *socket_name, const char *directory)
{
    void *socket = NULL;

    // validate the directory
    struct stat sr;
    check((stat((const char*)directory, &sr) == 0), "could not stat %s", directory);
    check(S_ISDIR(sr.st_mode), "%s is not a directory.", directory);

    // set up zeromq
    check(context, "Context has not been initialized");

    socket = zmq_socket(context, ZMQ_REP);
    check((socket != NULL), "Could not create zmq socket");
    check((zmq_bind(socket, socket_name) == 0), "could not bind to socket %s", socket_name);

    log_info("Serving directory <%s> on <%s>", directory, socket_name);


    while (1) {
        zmq_msg_t request;     
        zmq_msg_t reply;       

        zmq_msg_init (&request);        

        zmq_recv (socket, &request, 0);
        debug("Received a message\n");    
        zmq_msg_close (&request);

        //  Do some 'work'     
        /*
        respone = new response()
        Serve_handle(request, &response)
         */

        //  Send reply back to client   
        zmq_msg_init_data (&reply, "World", 5, NULL, NULL);
        zmq_send (socket, &reply, 0);
        zmq_msg_close (&reply);
    }

    return 0;

error:
    
    if (socket != NULL) {
        zmq_close(socket);   
        socket = NULL;
    }

    return -1;
}


