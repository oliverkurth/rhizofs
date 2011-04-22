
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h> // for optind, getopt
#include <pthread.h>

#include "dbg.h"
#include "servedir.h"

#define DEFAULT_DIRECTORY "."
#define DEFAULT_SOCKET "tcp://0.0.0.0:15656"
#define WORKER_SOCKET "inproc://workers"
#define NUM_WORKER_THREADS 5

typedef struct WorkerParams {
    char * directory;
    void * context;
} WorkerParams;


FILE *LOG_FILE = NULL;

/** zmq context */
static void * context = NULL;
static void * in_socket = NULL;
static void * worker_socket = NULL;
static int exit_code = EXIT_SUCCESS;
static pthread_t workers[NUM_WORKER_THREADS] = {(pthread_t)NULL};



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


void
shutdown(int UNUSED_PARAMETER(sig))
{
    fprintf(stdout, "Shuting down...\n");

    // terminating the zmq_context will make all
    // sockets exit with errno == ETERM
    if (context != NULL) {
        zmq_term(context);
        context = NULL;
    }

    if (in_socket != NULL) {
        zmq_close(in_socket);
        in_socket = NULL;
    }

    if (worker_socket != NULL) {
        zmq_close(worker_socket);
        worker_socket = NULL;
    }

    pthread_exit(NULL);
    exit(exit_code);
}


void *
worker_routine(void * wp)
{
    ServeDir * sd = NULL;
    WorkerParams * workerparams = (WorkerParams *) wp;

    sd = ServeDir_create(workerparams->context, WORKER_SOCKET, workerparams->directory);
    check((sd != NULL), "error serving directory.");
    // TODO: shutdown on error
    ServeDir_serve(sd);
    
    ServeDir_destroy(sd);
    pthread_exit(NULL);

error:
    if (sd != NULL) {
        ServeDir_destroy(sd);
    }
    pthread_exit(NULL);
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

    // initialize the zmq context
    context = zmq_init(1);
    check((context != NULL), "Could not create Zmq context");

    // install signal handler
    (void)signal(SIGTERM, shutdown);
    (void)signal(SIGINT, shutdown);

    // create the sockets
    in_socket = zmq_socket (context, ZMQ_XREP);
    check((in_socket != NULL), "Could not create zmq socket");
    check((zmq_bind(in_socket, socket_name) == 0), "could not bind to socket %s", socket_name);

    //  Socket to talk to workers
    worker_socket = zmq_socket (context, ZMQ_XREQ);
    check((worker_socket != NULL), "Could not create internal zmq worker socket");
    check((zmq_bind(worker_socket, WORKER_SOCKET) == 0), "could not bind to socket %s", WORKER_SOCKET);

 
    // startup the worker threads
    WorkerParams workerparams;
    workerparams.context = context;
    workerparams.directory = directory;
    int t = 0;
    for (t=0; t<NUM_WORKER_THREADS; ++t) {
        pthread_create(&workers[t], NULL, worker_routine, (void *) &workerparams);   
    }

    // connect the worker threads to the incomming socket
    check((zmq_device(ZMQ_QUEUE, in_socket, worker_socket) == 0), "Could not set up queue between sockets");

    shutdown(SIGTERM);
    exit(exit_code); // surpress compiler warnings

error:

    exit_code = EXIT_FAILURE;
    shutdown(0);
    exit(exit_code); // surpress compiler warnings
}
