#ifndef __server_request_h__
#define __server_request_h__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

#include <zmq.h>

#include "mapping.h"
#include "version.h"
#include "proto/rhizofs.pb-c.h"


/**
 * create and allocate a new request struct
 *
 * returns NULL on error
 */
Rhizofs__Request * Request_create();


/**
 */
void Request_destroy(Rhizofs__Request * request);


/**
 * initialize a pre-allocated Request struct
 *
 * returns boolean true on success, otherwise false
 */
bool Request_init(Rhizofs__Request * request);

/**
 * de-initialize a pre-allocated request struct.
 * the request-struct itself will not be freed
 */
void Request_deinit(Rhizofs__Request * request);

/**
 * pack the request in a zmq message
 * the message will be initialized to the correct size
 * and has to be zmq_msg_closed
 *
 * true = success
 */
bool Request_pack(const Rhizofs__Request * request, zmq_msg_t * msg);


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

/**
 * passed data will not be freed
 *
 * returns true on success, otherwise false
 */
bool Request_set_data(Rhizofs__Request * response, const uint8_t * data, size_t len);

/**
 * check if a request has any data associated with it
 *
 * returns the length of the if there is data, and -1 if there
 * is no datablock
 */
int Request_has_data(Rhizofs__Request * response);




#endif /* __server_request_h__ */
