#include "response.h"
#include "datablock.h"

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

    // initialize pointers to NULL
    response->datablock = NULL;

    return response;

error:
    free(version);
    free(response);
    return NULL;
}


void
Response_destroy(Rhizofs__Response * response)
{

    if (response) {
        Attrs_destroy(response->attrs);

        if (response->n_directory_entries != 0) {
            int i = 0;
            for (i=0; i<(int)response->n_directory_entries; i++) {
                free(response->directory_entries[i]);
            }
            free(response->directory_entries);
        }

        if (response->datablock != NULL) {
            DataBlock_destroy(response->datablock);
        }

        free(response->version);
        free(response);
    }
    response = NULL;
}


bool
Response_pack(const Rhizofs__Response * response, zmq_msg_t * msg)
{
    /* serialize the reply */
    size_t len = (size_t)rhizofs__response__get_packed_size(response);
    debug("Response will be %d bytes long", (int)len);

    check((zmq_msg_init_size(msg, len) == 0), "Could not initialize message");
    check((rhizofs__response__pack(response, zmq_msg_data(msg)) == len),
            "Could not pack message");

    return true;

error:
    zmq_msg_close(msg);
    return false;
}


bool
Response_set_data(Rhizofs__Response * response, const uint8_t * data, size_t len)
{
    Rhizofs__DataBlock * datablock = NULL;

    check((response->datablock == NULL), "Response has aleady a data block");

    datablock = DataBlock_create();
    check_mem(datablock);

    check(( DataBlock_set_data(datablock, data, len,
           RHIZOFS__COMPRESSION_TYPE__COMPR_LZ4) == true), "could not set datablock data");

    response->datablock = datablock;

    return true;

error:
    DataBlock_destroy(datablock);
    return false;
}


void
Response_set_errno(Rhizofs__Response * response, int eno)
{
    int perrno = Errno_from_local(eno);
    debug("Setting local errno %d as protocol errno %d", eno, perrno);
    response->errnotype = perrno;
}


int
Response_get_errno(const Rhizofs__Response * response)
{
    int eno = Errno_to_local( response->errnotype );
    debug("Getting protocol errno %d as local errno %d", 
                response->errnotype, eno);
    return eno;
}


Rhizofs__Response *
Response_from_message(zmq_msg_t *msg)
{
    Rhizofs__Response *response = NULL;

    debug("Response is %d bytes long", (int)zmq_msg_size(msg));

    response = rhizofs__response__unpack(NULL,
        zmq_msg_size(msg),
        zmq_msg_data(msg));

    return response;
}


void
Response_from_message_destroy(Rhizofs__Response * response)
{
    if (response != NULL) {
        rhizofs__response__free_unpacked(response, NULL);
    }
}


int
Response_has_data(Rhizofs__Response * response)
{
    if (response != NULL) {
        if (response->datablock != NULL) {
            return response->datablock->size;;
        }
    }

    return -1;
}
