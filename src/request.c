#include "request.h"

#include "dbg.h"
#include "mapping.h"

Rhizofs__Request *
Request_from_message(zmq_msg_t *msg)
{
    Rhizofs__Request *request = NULL;

    debug("Request is %d bytes long", (int)zmq_msg_size(msg));

    request = rhizofs__request__unpack(NULL,
        zmq_msg_size(msg),
        zmq_msg_data(msg));

    return request;
}


void
Request_from_message_destroy(Rhizofs__Request * request)
{
    if (request != NULL) {
        rhizofs__request__free_unpacked(request, NULL);
    }
}


Rhizofs__Request *
Request_create()
{
    Rhizofs__Request * request = NULL;
    Rhizofs__Version * version = NULL;

    request = calloc(sizeof(Rhizofs__Request), 1);
    check_mem(request);
    rhizofs__request__init(request);
    request->openflags = NULL;

    version = calloc(sizeof(Rhizofs__Version), 1);
    check_mem(version);
    rhizofs__version__init(version);

    version->major = RHI_VERSION_MAJOR;
    version->minor = RHI_VERSION_MINOR;

    request->version = version;

    return request;

error:
    free(version);
    free(request);
    return NULL;
}


void
Request_destroy(Rhizofs__Request * request)
{
    if (request) {
        free(request->version);
        free(request->openflags);
        free(request->permissions);
        free(request);
    }
    request = NULL;
}


bool
Request_pack(const Rhizofs__Request * request, zmq_msg_t * msg)
{
    /* serialize the reply */
    size_t len = (size_t)rhizofs__request__get_packed_size(request);
    debug("Request will be %d bytes long", (int)len);

    check((zmq_msg_init_size(msg, len) == 0), "Could not initialize message");
    check((rhizofs__request__pack(request, zmq_msg_data(msg)) == len), "Could not pack message");

    return true;

error:
    zmq_msg_close(msg);
    return false;
}
