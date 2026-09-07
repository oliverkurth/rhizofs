/*
 * minimal raw protocol client used by the pytest regression tests
 * to send hand-crafted (and possibly malicious) requests directly
 * to a running rhizosrv instance, bypassing the FUSE client and the
 * kernel's path/size sanitization it would otherwise apply.
 *
 * usage:
 *   rhizo-rawclient <endpoint> ping
 *   rhizo-rawclient <endpoint> getattr <path>
 *   rhizo-rawclient <endpoint> readdir <path>
 *   rhizo-rawclient <endpoint> read <path> <size> <offset>
 *   rhizo-rawclient <endpoint> rename <path> <path_to>
 *   rhizo-rawclient <endpoint> writeraw <path> <declared_size> <hex_bytes>
 *
 * all subcommands print a single line of "KEY=VALUE" pairs to stdout
 * and exit with 0 if a reply was received (regardless of the
 * errno reported in the reply) or 1 on a local/timeout error.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <zmq.h>

#include "../src/request.h"
#include "../src/response.h"
#include "../src/datablock.h"
#include "../src/proto/rhizofs.pb-c.h"

#define RECV_TIMEOUT_MS 5000

static void *
connect_socket(void *ctx, const char *endpoint)
{
    void *sock = zmq_socket(ctx, ZMQ_REQ);
    if (sock == NULL) {
        fprintf(stderr, "could not create socket\n");
        exit(1);
    }

    int timeout = RECV_TIMEOUT_MS;
    zmq_setsockopt(sock, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    zmq_setsockopt(sock, ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    int linger = 0;
    zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(linger));

    if (zmq_connect(sock, endpoint) != 0) {
        fprintf(stderr, "could not connect to %s\n", endpoint);
        exit(1);
    }

    return sock;
}

static Rhizofs__Response *
send_request(void *sock, Rhizofs__Request *request)
{
    zmq_msg_t msg_req;
    zmq_msg_t msg_rep;

    if (Request_pack(request, &msg_req) != true) {
        fprintf(stderr, "could not pack request\n");
        exit(1);
    }

    if (zmq_msg_send(&msg_req, sock, 0) == -1) {
        fprintf(stderr, "could not send request: %s\n", strerror(errno));
        exit(1);
    }

    if (zmq_msg_init(&msg_rep) != 0) {
        fprintf(stderr, "could not init reply message\n");
        exit(1);
    }

    if (zmq_msg_recv(&msg_rep, sock, 0) == -1) {
        printf("TIMEOUT=1\n");
        exit(1);
    }

    Rhizofs__Response *response = Response_from_message(&msg_rep);
    zmq_msg_close(&msg_rep);

    if (response == NULL) {
        fprintf(stderr, "could not unpack response\n");
        exit(1);
    }

    return response;
}

static int
decode_hex(const char *hex, uint8_t **out)
{
    size_t hexlen = strlen(hex);
    if (hexlen % 2 != 0) {
        fprintf(stderr, "hex string must have an even length\n");
        exit(1);
    }

    size_t len = hexlen / 2;
    uint8_t *buf = malloc(len > 0 ? len : 1);
    for (size_t i = 0; i < len; i++) {
        unsigned int byte;
        if (sscanf(hex + (i * 2), "%2x", &byte) != 1) {
            fprintf(stderr, "invalid hex string\n");
            exit(1);
        }
        buf[i] = (uint8_t)byte;
    }

    *out = buf;
    return (int)len;
}

static int
cmd_ping(void *sock)
{
    Rhizofs__Request *request = Request_create();
    request->requesttype = RHIZOFS__REQUEST_TYPE__PING;

    Rhizofs__Response *response = send_request(sock, request);

    printf("ERRNO=%d\n", response->errnotype);

    Response_from_message_destroy(response);
    Request_destroy(request);
    return 0;
}

static int
cmd_getattr(void *sock, const char *path)
{
    Rhizofs__Request *request = Request_create();
    request->requesttype = RHIZOFS__REQUEST_TYPE__GETATTR;
    request->path = strdup(path);

    Rhizofs__Response *response = send_request(sock, request);

    printf("ERRNO=%d\n", response->errnotype);
    printf("ATTRS=%d\n", response->attrs != NULL ? 1 : 0);
    if (response->attrs != NULL) {
        printf("SIZE=%lld\n", (long long)response->attrs->size);
    }

    Response_from_message_destroy(response);
    Request_destroy(request);
    return 0;
}

static int
cmd_readdir(void *sock, const char *path)
{
    Rhizofs__Request *request = Request_create();
    request->requesttype = RHIZOFS__REQUEST_TYPE__READDIR;
    request->path = strdup(path);

    Rhizofs__Response *response = send_request(sock, request);

    printf("ERRNO=%d\n", response->errnotype);
    printf("ENTRIES=%zu\n", response->n_directory_entries);

    Response_from_message_destroy(response);
    Request_destroy(request);
    return 0;
}

static int
cmd_read(void *sock, const char *path, int64_t size, int64_t offset)
{
    Rhizofs__Request *request = Request_create();
    request->requesttype = RHIZOFS__REQUEST_TYPE__READ;
    request->path = strdup(path);
    request->has_size = 1;
    request->size = size;
    request->has_offset = 1;
    request->offset = offset;

    Rhizofs__Response *response = send_request(sock, request);

    printf("ERRNO=%d\n", response->errnotype);
    if (response->datablock != NULL) {
        /* datablock->size is the actual (uncompressed) amount of data
         * read; datablock->data.len is the size of the (possibly LZ4
         * compressed) bytes on the wire and is not useful to determine
         * how many bytes the server actually read from the file */
        printf("SIZE=%lld\n", (long long)response->datablock->size);
    }
    else {
        printf("SIZE=-1\n");
    }

    Response_from_message_destroy(response);
    Request_destroy(request);
    return 0;
}

static int
cmd_rename(void *sock, const char *path, const char *path_to)
{
    Rhizofs__Request *request = Request_create();
    request->requesttype = RHIZOFS__REQUEST_TYPE__RENAME;
    request->path = strdup(path);
    request->path_to = strdup(path_to);

    Rhizofs__Response *response = send_request(sock, request);

    printf("ERRNO=%d\n", response->errnotype);

    Response_from_message_destroy(response);
    Request_destroy(request);
    return 0;
}

static int
cmd_writeraw(void *sock, const char *path, int64_t declared_size, const char *hex)
{
    uint8_t *rawdata = NULL;
    int rawlen = decode_hex(hex, &rawdata);

    Rhizofs__Request *request = Request_create();
    request->requesttype = RHIZOFS__REQUEST_TYPE__WRITE;
    request->path = strdup(path);
    request->has_size = 1;
    request->size = declared_size;
    request->has_offset = 1;
    request->offset = 0;

    Rhizofs__DataBlock *dblk = DataBlock_create();
    dblk->size = declared_size;
    dblk->data.data = rawdata;
    dblk->data.len = (size_t)rawlen;
    dblk->compression = RHIZOFS__COMPRESSION_TYPE__COMPR_LZ4;
    request->datablock = dblk;

    Rhizofs__Response *response = send_request(sock, request);

    printf("ERRNO=%d\n", response->errnotype);
    printf("HASSIZE=%d\n", response->has_size);
    if (response->has_size) {
        printf("SIZE=%lld\n", (long long)response->size);
    }

    Response_from_message_destroy(response);
    Request_destroy(request);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <endpoint> <ping|getattr|readdir|read|rename|writeraw> [args...]\n", argv[0]);
        return 1;
    }

    const char *endpoint = argv[1];
    const char *cmd = argv[2];

    void *ctx = zmq_ctx_new();
    void *sock = connect_socket(ctx, endpoint);

    int rc;
    if (strcmp(cmd, "ping") == 0) {
        rc = cmd_ping(sock);
    }
    else if (strcmp(cmd, "getattr") == 0 && argc == 4) {
        rc = cmd_getattr(sock, argv[3]);
    }
    else if (strcmp(cmd, "readdir") == 0 && argc == 4) {
        rc = cmd_readdir(sock, argv[3]);
    }
    else if (strcmp(cmd, "read") == 0 && argc == 6) {
        rc = cmd_read(sock, argv[3], strtoll(argv[4], NULL, 10), strtoll(argv[5], NULL, 10));
    }
    else if (strcmp(cmd, "rename") == 0 && argc == 5) {
        rc = cmd_rename(sock, argv[3], argv[4]);
    }
    else if (strcmp(cmd, "writeraw") == 0 && argc == 6) {
        rc = cmd_writeraw(sock, argv[3], strtoll(argv[4], NULL, 10), argv[5]);
    }
    else {
        fprintf(stderr, "unknown command or wrong number of arguments: %s\n", cmd);
        rc = 1;
    }

    zmq_close(sock);
    zmq_ctx_term(ctx);

    return rc;
}
