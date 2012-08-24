#include "datablock.h"

#include "dbg.h"
#include "lz4.h"

// prototypes
static int get_uncompressed_data(Rhizofs__DataBlock * dblk, uint8_t ** data, int do_alloc);
static int get_lz4_compressed_data(Rhizofs__DataBlock * dblk, uint8_t ** data, int do_alloc);
static int set_lz4_compressed_data(Rhizofs__DataBlock * dblk, const uint8_t * data, const size_t len);



Rhizofs__DataBlock *
DataBlock_create()
{
    Rhizofs__DataBlock * datablock = NULL;

    datablock = calloc(sizeof(Rhizofs__DataBlock), 1);
    check_mem(datablock);
    rhizofs__data_block__init(datablock);
    datablock->data.data = NULL;
    datablock->data.len = 0;

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
            dblk->data.data = NULL;
        }
        free(dblk);
    }
    dblk = NULL;
}


bool
DataBlock_set_data(Rhizofs__DataBlock * dblk, const uint8_t * data,
        size_t len, Rhizofs__CompressionType compression)
{
    check((dblk != NULL), "passed datablock is null");
    check((len != 0), "passed length of datablock is 0");

    // only use compression if the length of the data exceeds
    // a threshold as tiny chunks of data are not very compression-worth
    // anyways
    Rhizofs__CompressionType compression_method = 
            len > 100 ? compression : RHIZOFS__COMPRESSION_TYPE__COMPR_NONE;

    bool compression_failed = false;
    switch (compression_method) {
        case RHIZOFS__COMPRESSION_TYPE__COMPR_NONE:
            // skip. data will be added in fallback
            break;

        case RHIZOFS__COMPRESSION_TYPE__COMPR_LZ4:
            {
                if (set_lz4_compressed_data(dblk, data, len) != -1) {
                    dblk->compression = compression_method;
                }
                else {
                    compression_failed = true;
                    debug("could not lz4 compress");
                }
            }
            break;

        default:
            log_and_error("Unsupported compression type %d", compression_method);
    }

    // fallback to no compression
    if ((compression_method == RHIZOFS__COMPRESSION_TYPE__COMPR_NONE) || 
                (compression_failed == true )) {
        dblk->data.data = NULL;
        dblk->data.data = malloc((size_t)(len * sizeof(uint8_t)));
        check_mem(dblk->data.data);
        memcpy(dblk->data.data, data, (size_t)(len * sizeof(uint8_t)));
        dblk->data.len = len;
        dblk->compression = RHIZOFS__COMPRESSION_TYPE__COMPR_NONE;
    }

#ifdef DEBUG
    if (len != 0) {
        debug("compressed datablock to %d%% of original size (%db -> %db)",
                (int)((100*dblk->data.len) / len), (int)len, (int)dblk->data.len);
    }
#endif

    dblk->size = len; // uncompressed size of the data
    return true;

error:

    if (dblk) {
        free(dblk->data.data);
        dblk->data.data = NULL;
        dblk->data.len = 0;
    }
    return false;
}


int
DataBlock_get_data(Rhizofs__DataBlock * dblk, uint8_t ** data)
{
    int len = 0;

    check((dblk != NULL), "passed datablock is null");

    debug("Getting data from datablock containing %d bytes of compressed data",
            (int)dblk->size);

    switch (dblk->compression) {
        case RHIZOFS__COMPRESSION_TYPE__COMPR_NONE:
            {
                len = get_uncompressed_data(dblk, data, 1);
            }
            break;

        case RHIZOFS__COMPRESSION_TYPE__COMPR_LZ4:
            {
                len = get_lz4_compressed_data(dblk, data, 1);
            }
            break;

        default:
            log_and_error("Unsupported compression type %d", dblk->compression);
    }

    debug("Getting data from datablock containing %d bytes "
            "of UNcompressed data", (int)len);
    check((len > -1), "could not copy uncompressed data");

    return len;

error:
    free(data);
    data = NULL;
    return -1;
}


int
DataBlock_get_data_noalloc(Rhizofs__DataBlock * dblk, uint8_t * data, size_t data_len)
{
    int len = 0;

    check((dblk != NULL), "passed datablock is null");
    check((data != NULL), "passed data buffer is null");
    check((data_len >= (size_t)dblk->size), "passed data buffer is too small "
        "for contents of datablock"
        "(length of data=%d; buffer size=%d)", (int)dblk->size, (int)data_len);

    debug("Getting data from datablock containing %d bytes "
        "of compressed data", (int)dblk->data.len);

    switch (dblk->compression) {
        case RHIZOFS__COMPRESSION_TYPE__COMPR_NONE:
            {
                len = get_uncompressed_data(dblk, &data, 0);
            }
            break;

        case RHIZOFS__COMPRESSION_TYPE__COMPR_LZ4:
            {
                len = get_lz4_compressed_data(dblk, &data, 0);
            }
            break;

        default:
            log_and_error("Unsupported compression type %d", dblk->compression);
    }

    debug("Getting data from datablock containing %d bytes of UNcompressed data", (int)len);
    check((len > -1), "could not copy uncompressed data");

    return len;

error:
    return -1;
}


/*** uncompressed  *************************************/

/**
 * get data from a datablock
 *
 * do_alloc indicates if the buffer "data" should be allocated by this function
 * or already comes preallocated. if it is preallocated a size of at least dblk->size
 * is asumed.
 *
 * returns length of data or -1 on failure
 */
static int
get_uncompressed_data(Rhizofs__DataBlock * dblk, uint8_t ** data, int do_alloc)
{
    size_t len = dblk->data.len;
    bool free_data = false;

    check((dblk != NULL), "passed datablock is null");
    check((data != NULL), "passed data pointer is null");

    if (do_alloc) {
        (*data) = calloc(sizeof(uint8_t), len);
        free_data = true;
    }
    check(((*data) != NULL), "data buffer is null");
    memcpy((*data), dblk->data.data, len);

    return len;

error:

    if (free_data) {
        free(*data);
    }

    return -1;
}



/*** LZ4 Compression *************************************/

/**
 * compress the given data into the datablock
 *
 * returns the number of compressed bytes on success
 * and -1 on failure
 **/
static int
set_lz4_compressed_data(Rhizofs__DataBlock * dblk, const uint8_t * data, const size_t len)
{
    size_t bytes_compressed = 0;

    check((dblk != NULL), "passed datablock is null");
    check((data != NULL), "passed data is null");

    /*
     * from http://fastcompression.blogspot.de/2011/05/lz4-explained.html :
     *      There can be any number of bytes following the token.
     *      There is no "size limit". As a sidenote, here is the reason why a
     *      not-compressible input stream can be expanded by up to 0.4%.
     *
     *  so we will size the outputbuffer accordingly to prevent writes
     *  outside the allocated memory area
     **/
    int extra_mem_size = 0;
    if (len > 0) {
        extra_mem_size = len / 250;
    }
    dblk->data.data = malloc(sizeof(uint8_t) * (len + extra_mem_size));
    check_mem(dblk->data.data);

    bytes_compressed = LZ4_compress((const char*)data, (char*)dblk->data.data, len);
    check_debug((bytes_compressed != 0), "LZ4_compress failed");

    dblk->data.len = bytes_compressed;

    return bytes_compressed;

error:
    if (dblk != NULL) {
        if (dblk->data.data != NULL) {
            free(dblk->data.data);
            dblk->data.data = NULL;
        }
        dblk->data.len = 0;
    }
    return -1;
}

/**
 * get the uncompressed data from a lz4 compressed datablock
 *
 * see  get_uncompressed_data(Rhizofs__DataBlock * dblk, uint8_t * data, int do_alloc)
 */
static int
get_lz4_compressed_data(Rhizofs__DataBlock * dblk, uint8_t ** data, int do_alloc)
{
    size_t len = dblk->size;
    int bytes_uncompressed = 0;
    bool free_data = false;

    check((dblk != NULL), "passed datablock is null");
    check((data != NULL), "passed data pointer is null");

    if (do_alloc) {
        (*data) = calloc(sizeof(uint8_t), len);
        free_data = true;
    }
    check(((*data) != NULL), "data buffer is null");

    bytes_uncompressed = LZ4_uncompress((const char*)dblk->data.data,
                (char*)(*data), len);
    check((bytes_uncompressed >= 0), "LZ4_uncompress failed");
    check((dblk->data.len == (size_t)bytes_uncompressed), "could not decompress "
                "the whole block (only %d bytes of %d bytes)", 
                bytes_uncompressed, (int)dblk->data.len);

    return len;

error:

    if (free_data) {
        free(*data);
    }
    return -1;
}
