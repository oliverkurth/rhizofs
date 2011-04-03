
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h> // for optind, getopt

#include "dbg.h"
#include "serve.h"

#define DEFAULT_DIRECTORY "."
#define DEFAULT_SOCKET "tcp://0.0.0.0:15656"


FILE *LOG_FILE = NULL;


void
print_wrong_arg(const char *msg)
{
    fprintf(stderr, "Error parsing parameters: %s\n\
Use --help for help.\n", msg);
}

void
print_usage()
{
    fprintf(stdout, "prgram <socket> <directory>\n");
}


int
main(int argc, char *argv[])
{
    char *socket_name = DEFAULT_SOCKET; // name of the zeromq socket
    char *directory = DEFAULT_DIRECTORY;   // name of the directory to server
    int i;

    // log to stdout
    LOG_FILE = stdout;

    // skip the program name
    argc -= optind;
    argv += optind;

    if (argc == 0) {
        print_wrong_arg("No or wrong parameters given.");
        print_usage();
        goto error;
    }


    i = 0;
    do {
        if (*argv) {

            switch (i) {
                case 0:
                    socket_name = (*argv);
                    debug("socket_name : %s", socket_name);
                    break;

                case 1:
                    directory = (*argv);
                    debug("directory : %s", directory);
                    break;

                default:
                    print_wrong_arg("No or wrong parameters given.");
                    print_usage();
                    goto error;
            }

            ++argv;
            ++i;
        }
    } while (*argv);

    check((Serve_init() == 0), "Could not create server");

    check((Serve_directory(socket_name, directory) == 0), "error serving directory.");

    Serve_destroy();

    return 0;

error:

    Serve_destroy();
    return -1;
}
