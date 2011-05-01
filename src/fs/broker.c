#include "broker.h"

#include "../dbg.h"

int
Broker_run(void * context, const char * remote_socket_name, const char * internal_socket_name)
{

    void * remote_socket = NULL;
    void * internal_socket = NULL;

    // create the sockets
    debug("Broker: remote_socket_name: %s", remote_socket_name);
    remote_socket = zmq_socket(context, ZMQ_XREQ);
    check((remote_socket != NULL), "Could not create zmq socket to connect to remote");
    check((zmq_connect(remote_socket, remote_socket_name) == 0), "could not bind to socket %s", remote_socket_name);

    //  Socket to talk to fs threads
    debug("Broker: internal_socket_name: %s", internal_socket_name);
    internal_socket = zmq_socket (context, ZMQ_XREP);
    check((internal_socket != NULL), "Could not create internal fuse thread socket");
    check((zmq_bind(internal_socket, internal_socket_name) == 0), "could not bind to socket %s", internal_socket_name);

    debug("starting broker queue");
    if (zmq_device(ZMQ_QUEUE, remote_socket, internal_socket) != 0) {
        if (errno == ETERM) {
            debug("context has been terminated");
        }
        else {
            log_err("Could not set up queue between sockets");
        }
    }

    zmq_close(remote_socket);
    zmq_close(internal_socket);

    return 0;

error:

    if (remote_socket != NULL) {
        zmq_close(remote_socket);
    }
    if (internal_socket != NULL) {
        zmq_close(internal_socket);
    }


    return -1;
}
