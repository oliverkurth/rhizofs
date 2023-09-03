#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zmq.h>


int main (int argc, char *argv[])
{
    char public_key[41];
    char secret_key[41];
    FILE *fptr;
    char *fname_pub;
    char fname_secret[PATH_MAX];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <public key file name>\n", argv[0]);
        fprintf(stderr, "Generates a zmq curve key pair. The public key will be stored\n"
                        "in the file given. The secret key will be stored in the file\n"
                        "with the same name but with '.secret' appended.\n");
        exit(1);
    }

    if (zmq_curve_keypair(public_key, secret_key)) {
        if (zmq_errno() == ENOTSUP)
            printf("To use %s, please install libsodium and then "
                  "rebuild libzmq.", argv[0]);
        exit (2);
    }

    umask(0066);

    fname_pub = argv[1];
    fptr = fopen(fname_pub, "wt");
    if (fptr) {
        fprintf(fptr, "%s", public_key);
        fclose(fptr);
    } else {
        fprintf(stderr, "failed to open file %s for writing %s (%d):\n",
                fname_pub, strerror(errno), errno);
        exit(3);
    }

    snprintf(fname_secret, sizeof(fname_secret), "%s.secret", fname_pub);
    fptr = fopen(fname_secret, "wt");
    if (fptr) {
        fprintf(fptr, "%s", secret_key);
        fclose(fptr);
    } else {
        fprintf(stderr, "failed to open file %s for writing %s (%d):\n",
                fname_secret, strerror(errno), errno);
        exit(4);
    }

    exit (0);
}