#ifndef __server_response_h_
#define __server_response_h_

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "dbg.h"
#include "version.h"
#include "proto/rhizofs.pb-c.h"


Rhizofs__Response * Response_create();
void Response_destroy(Rhizofs__Response * response);


#endif //__server_response_h_
