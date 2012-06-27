#ifndef __server_response_h__
#define __server_response_h__

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

#include <zmq.h>
#include "version.h"
#include "mapping.h"
#include "proto/rhizofs.pb-c.h"


Rhizofs__Response * Response_create();
void Response_destroy(Rhizofs__Response * response);

/**
 * set the local errno
 * setting will convert the value to the corresponding protocol-errno
 * value
 */
void Response_set_errno(Rhizofs__Response ** response, int eno);


/**
 * set the local errno
 */
int Response_get_errno(const Rhizofs__Response * response);


/**
 * pack the response in a zmq message
 * the message will be initialized to the correct size
 * and has to be zmq_msg_closed
 *
 * 0 = success
 */
int Response_pack(const Rhizofs__Response * response, zmq_msg_t * msg);

/**
 * will not make a copy of the data block, but destroying the response will also
 * free the data
 *
 * returns true on success, otherwise false
 */
bool Response_set_data(Rhizofs__Response ** response, uint8_t * data, size_t len);

Rhizofs__Response * Response_from_message(zmq_msg_t *msg);

void Response_from_message_destroy(Rhizofs__Response * response);

/**
 * check if a response has any data associated with it
 *
 * returns the lenght of the if there is data, and -1 if there
 * is no datablock
 */
int Response_has_data(Rhizofs__Response * response);

#endif /* __server_response_h__ */
