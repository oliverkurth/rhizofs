#ifndef __server_request_h__
#define __server_request_h__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include <zmq.h>

#include "mapping.h"
#include "version.h"
#include "proto/rhizofs.pb-c.h"


Rhizofs__Request * Request_create();

void Request_destroy(Rhizofs__Request * request);

/**
 * pack the request in a zmq message
 * the message will be initialized to the correct size
 * and has to be zmq_msg_closed
 *
 * 0 = success
 */
int Request_pack(const Rhizofs__Request * request, zmq_msg_t * msg);




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

#endif /* __server_request_h__ */
