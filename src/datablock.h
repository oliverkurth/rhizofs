#ifndef __datablock_h__
#define __datablock_h__

#include <stdlib.h>
#include <stdint.h>
#include "proto/rhizofs.pb-c.h"


/**
 * allocate a new datablock
 *
 * returns NULL on failure
 **/
Rhizofs__DataBlock * DataBlock_create();


/**
 * free all memory allocated by a datablock
 */
void DataBlock_destroy(Rhizofs__DataBlock * dblk);


/**
 * set the data of the datablock
 *
 * the function will take ownership of
 * the allocated memory of passed data pointer
 * and will free it
 *
 * returns 0 on success and -1 on failure
 */
int DataBlock_set_data(Rhizofs__DataBlock * dblk, uint8_t * data, 
        size_t len, Rhizofs__CompressionType compression);


/**
 * write the data stored in the datablock to the buffer "data"
 *
 * this function allocates the memory for "data" and the caller is 
 * responsible to free it.
 *
 * returns the number of bytes of "data" or -1 on failure
 */
int DataBlock_get_data(Rhizofs__DataBlock * dblk, uint8_t * data);


/**
 * write the data stored in the datablock to the buffer "data"
 *
 * expects "data" to be already allocated to the size of
 * dblk->size
 *
 * returns the number of bytes of "data" or -1 on failure
 */
int DataBlock_get_data_noalloc(Rhizofs__DataBlock * dblk, uint8_t * data, size_t len_data);



#endif
