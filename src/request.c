#include "request.h"
#include "datablock.h"

#include "dbg.h"

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

    version = calloc(sizeof(Rhizofs__Version), 1);
    check_mem(version);
    rhizofs__version__init(version);

    version->major = RHI_VERSION_MAJOR;
    version->minor = RHI_VERSION_MINOR;

    request->version = version;

    // initialize pointers to NULL
    request->datablock = NULL;

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
        if (request->datablock != NULL) {
            DataBlock_destroy(request->datablock);
        }
    }

    free(request);
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


bool
Request_set_data(Rhizofs__Request * request, const uint8_t * data, size_t len)
{
    Rhizofs__DataBlock * datablock = NULL;

    check((request->datablock == NULL), "Request has aleady a data block");

    datablock = DataBlock_create();
    check_mem(datablock);

    check(( DataBlock_set_data(datablock, data, len,
           RHIZOFS__COMPRESSION_TYPE__COMPR_LZ4) == true), "could not set datablock data");

    request->datablock = datablock;

    return true;

error:
    DataBlock_destroy(datablock);
    return false;
}


int
Request_has_data(Rhizofs__Request * request)
{
    if (request != NULL) {
        if (request->datablock != NULL) {
            return request->datablock->size;;
        }
    }

    return -1;
}
