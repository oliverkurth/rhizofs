#include "response.h"


Rhizofs__Response *
Response_create() 
{
    Rhizofs__Response * response = calloc(sizeof(Rhizofs__Response), 1);
    check_mem(response);
    rhizofs__response__init(response);

    Rhizofs__Version * version = calloc(sizeof(Rhizofs__Version), 1); 
    check_mem(version);
    rhizofs__version__init(version);

    version->major = RHI_VERSION_MAJOR; 
    version->minor = RHI_VERSION_MINOR; 
    

    response->version = version;
    response->errortype = RHIZOFS__ERROR_TYPE__NONE; // be positive


    return response;

error:
    
    free(version);
    free(response);

    return NULL;
}


void
Response_destroy(Rhizofs__Response * response) {

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

/**
 * pack the response in a zmq message
 * the message will be initialized to the correct size
 * and has to be zmq_msg_closed
 *
 * 0 = success
 */
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
