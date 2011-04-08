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



