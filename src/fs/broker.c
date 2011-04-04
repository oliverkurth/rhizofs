#include "broker.h"



void
Broker_destroy() {

    if (fuse_socket != NULL) {
        zmq_close(fuse_socket);
        fuse_socket = NULL;
    }

    if (remote_socket != NULL) {
        zmq_close(remote_socket);
        remote_socket = NULL;
    }
}
