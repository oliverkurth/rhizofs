#include "response.h"

#include "dbg.h"

Rhizofs__Response *
Response_create()
{
    Rhizofs__Response * response = NULL;
    Rhizofs__Version * version = NULL;

    response = calloc(sizeof(Rhizofs__Response), 1);
    check_mem(response);
    rhizofs__response__init(response);

    version = calloc(sizeof(Rhizofs__Version), 1);
    check_mem(version);
    rhizofs__version__init(version);

    version->major = RHI_VERSION_MAJOR;
    version->minor = RHI_VERSION_MINOR;

    response->version = version;
    response->errnotype = RHIZOFS__ERRNO__ERRNO_NONE;

    return response;

error:

    free(version);
    free(response);

    return NULL;
}


void
Response_destroy(Rhizofs__Response * response)
{

    free(response->attrs);

    if (response->n_directory_entries != 0) {
        int i = 0;
        for (i=0; i<(int)response->n_directory_entries; i++) {
            free(response->directory_entries[i]);
        }
        free(response->directory_entries);
    }

    free(response->version);
    free(response);
    response = NULL;

}


int
Response_pack(const Rhizofs__Response * response, zmq_msg_t * msg)
{
    // serialize the reply
    size_t len = (size_t)rhizofs__response__get_packed_size(response);
    debug("Response will be %d bytes long", (int)len);

    check((zmq_msg_init_size(msg, len) == 0), "Could not initialize message");
    check((rhizofs__response__pack(response, zmq_msg_data(msg)) == len), "Could not pack message");

    return 0;
error:
    zmq_msg_close(msg);
    return -1;

}

void
Response_set_errno(Rhizofs__Response ** response, int eno)
{
    int perrno = mapping_errno_to_protocol(eno);

    debug("Setting protocol errno %d", perrno);
    (*response)->errnotype = perrno;

}

int
Response_get_errno(const Rhizofs__Response * response)
{
    return mapping_errno_from_protocol( response->errnotype );
}


Rhizofs__Response *
Response_from_message(zmq_msg_t *msg)
{
    Rhizofs__Response *response = NULL;

    debug("Response is %d bytes long", (int)zmq_msg_size(msg));

    response = rhizofs__response__unpack(NULL,
        zmq_msg_size(&(*msg)),
        zmq_msg_data(&(*msg)));

    return response;
}


void
Response_from_message_destroy(Rhizofs__Response * response)
{
    rhizofs__response__free_unpacked(response, NULL);
}


