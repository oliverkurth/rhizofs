#ifndef __server_request_h_
#define __server_request_h_

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include <zmq.h>
#include "mapping.h"
#include "proto/rhizofs.pb-c.h"


//Rhizofs__Request * Request_create();

/**
 * create an allocated request struct from a zmq_msg. returns NULL on
 * failure. the caller is responsible for freeing the struct
 * with Request_from_message_destroy
 */
Rhizofs__Request * Request_from_message(zmq_msg_t * msg);


/**
 * destroy/free a request deserialized with Request_from_message
 */
void Request_from_message_destroy(Rhizofs__Request * request);

#endif //__server_request_h_
