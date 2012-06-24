#include "datablock.h"

#include "dbg.h"

Rhizofs__DataBlock *
DataBlock_create()
{
    Rhizofs__DataBlock * datablock = NULL;

    datablock = calloc(sizeof(Rhizofs__DataBlock), 1);
    check_mem(datablock);
    rhizofs__data_block__init(datablock);

    return datablock;

error:
    free(datablock);
    return NULL;
}


void
DataBlock_destroy(Rhizofs__DataBlock * dblk)
{
    if (dblk != NULL) {
        if (dblk->data.data != NULL) {
            free(dblk->data.data);
        }
        free(dblk);
    }
}



int
DataBlock_set_data(Rhizofs__DataBlock * dblk, uint8_t * data, 
        size_t len, Rhizofs__CompressionType compression)
{
    check((dblk != NULL), "passed datablock is null");
    check((len != 0), "passed length of datablock is 0");

    switch (compression) {
        case RHIZOFS__COMPRESSION_TYPE__COMPR_NONE:
            dblk->data.len = len;
            dblk->data.data = data;
            break;

        default:
            log_and_error("Unsupported compression type %d", compression);
    }

    dblk->size = len;
    dblk->compression = compression;
    return 0;

error:
    return -1;
}


static int
get_uncompressed_data(Rhizofs__DataBlock * dblk, uint8_t * data, int do_alloc)
{
    size_t len = dblk->data.len;

    if (len > 0) {
        if (do_alloc) {
            data = calloc(sizeof(uint8_t), len);
        }
        check_mem(data);
        memcpy(data, dblk->data.data, len);
    }
    else {
        debug("got datablock with size < 1");
    }

    return len;

error:
    return -1;
}


int
DataBlock_get_data(Rhizofs__DataBlock * dblk, uint8_t * data)
{
    size_t len = 0;

    check((dblk != NULL), "passed datablock is null");

    switch (dblk->compression) {
        case RHIZOFS__COMPRESSION_TYPE__COMPR_NONE:
            {
                check((get_uncompressed_data(dblk, data, 1) != -1), "could not copy uncompressed data");
            }
            break;

        default:
            log_and_error("Unsupported compression type %d", dblk->compression);
    }

    return len;

error:
    if (data != NULL) {
        free(data);
    }
    return -1;
}


int
DataBlock_get_data_noalloc(Rhizofs__DataBlock * dblk, uint8_t * data, size_t data_len)
{
    int len = 0;

    check((dblk != NULL), "passed datablock is null");
    check((data != NULL), "passed data buffer is null");
    check((data_len >= dblk->size), "passed data buffer is too small "
        "for contents of datablock"
        "(length of data=%ld; buffer size=%ld)", dblk->size, data_len);

    switch (dblk->compression) {
        case RHIZOFS__COMPRESSION_TYPE__COMPR_NONE:
            {
                len = get_uncompressed_data(dblk, data, 0);
                check((len > -1), "could not copy uncompressed data");
            }
            break;

        default:
            log_and_error("Unsupported compression type %d", dblk->compression);
    }

    return len;

error:
    return -1;
}
