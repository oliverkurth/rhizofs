#include "dbg.h"
#include "request.h"


Rhizofs__Request *
Request_from_message(zmq_msg_t *msg)
{
    Rhizofs__Request *request = NULL;

    request = rhizofs__request__unpack(NULL,
        zmq_msg_size(&(*msg)),
        zmq_msg_data(&(*msg)));

    return request;
}


void
Request_from_message_destroy(Rhizofs__Request * request)
{
    rhizofs__request__free_unpacked(request, NULL);
}
