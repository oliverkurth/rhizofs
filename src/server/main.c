#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>

#include <zmq.h>

#include "../dbg.h"
#include "../version.h"
#include "servedir.h"

#define DEFAULT_N_WORKER_THREADS 5
#define MAX_N_WORKER_THREADS 200
#define WORKER_SOCKET "inproc://workers"

struct option opts_long[] = {
    {"numworkers", 1, 0, 'n'},
    {"help", 0, 0, 'h'},
    {"version", 0, 0, 'v'},
    {"verbose", 0, 0, 'V'},
    {0, 0, 0, 0}
};

static const char *opts_short = "hvn:V";

static const char *opts_desc =
    "  -n --numworkers=NUMBER  Number of worker threads to start [default=5]\n"
    "  -h --help\n"
    "  -v --version\n"
    "  -V --verbose\n";


typedef struct ServerSettings {
    char * directory;
    char * socketname;
    int n_worker_threads;
    bool verbose;
} ServerSettings;
static ServerSettings settings;


/** zmq context */
static void * context = NULL;

static void * in_socket = NULL;
static void * worker_socket = NULL;
static int exit_code = EXIT_SUCCESS;
static pthread_t * workers = NULL;


/* prototypes */
void print_wrong_arg(const char *);
void print_version();
void print_usage(const char *);
void shutdown(int);
void * worker_routine(void *);


void
print_wrong_arg(const char *msg)
{
    fprintf(stderr,
        "Error parsing parameters: %s"
        "\nUse --help for help.\n", msg
    );
    shutdown(0);
}


void
print_version()
{
    fprintf(stdout, RHI_NAME " v" RHI_VERSION_FULL "\n");
}


void
print_usage(const char * progname)
{
    fprintf(stderr,
        "%s SOCKET DIRECTORY\n"
        "\nIt's possible to specify all socket types supported by zeromq,"
        "\nalthough socket types like inproc (local in-process communication)"
        "\nare probably pretty useless for this case."
        "\n\nOptions:"
        "\n%s"
        "\nExample:"
        "\nServe the directory /home/myself/files on all network interfaces"
        "\non port 11555"
        "\n  rhizofs tcp://0.0.0.0:11555 /home/myself/files"
        "\n", progname, opts_desc
    );
}


void
shutdown(int sig)
{
    int t = 0;

    (void) sig;

    debug("Shuting down");

    if (in_socket != NULL) {
        zmq_close(in_socket);
        in_socket = NULL;
    }

    if (worker_socket != NULL) {
        zmq_close(worker_socket);
        worker_socket = NULL;
    }

    // terminating the zmq_context will make all
    // sockets exit with errno == ETERM
    if (context != NULL) {
        zmq_term(context);
        context = NULL;
    }

    if (workers != NULL) {
        while (t<settings.n_worker_threads) {
            pthread_join(workers[t], NULL);
            t++;
        }
        free(workers);
    }
    exit(exit_code);
}


void *
worker_routine(void * wp)
{
    ServeDir * sd = NULL;

    (void) wp;

    sd = ServeDir_create(context, WORKER_SOCKET,
            settings.directory);
    check((sd != NULL), "error serving directory.");
    // TODO: shutdown on error
    ServeDir_serve(sd);

    ServeDir_destroy(sd);
    pthread_exit(NULL);

error:
    ServeDir_destroy(sd);
    pthread_exit(NULL);
}


int
main(int argc, char *argv[])
{
    int optc;
    const char * progname = argv[0];

    /* log to stdout */
    dbg_set_logfile(stdout);

    /* defaults */
    settings.n_worker_threads = DEFAULT_N_WORKER_THREADS;
    settings.verbose = false;


    while ((optc = getopt_long(argc, argv, opts_short, opts_long, NULL)) != -1) {

        switch (optc) {
            case 'n':
                settings.n_worker_threads = atoi(optarg);
                if ((settings.n_worker_threads < 1)
                        || (settings.n_worker_threads > MAX_N_WORKER_THREADS))
                    {
                    print_wrong_arg("Illegal value for numworkers");
                }
                break;

            case 'h':
                print_usage(progname);
                shutdown(0);
                exit(0);
                break;

            case 'v':
                print_version();
                shutdown(0);
                exit(0);
                break;

            case 'V':
                settings.verbose = true;
                break;

            default:
                print_wrong_arg("Unknown option");
                break;
        }
    }

    debug("optind:%d; argv:%d", optind, argc);
    if (argc < (optind + 2)) {
        print_wrong_arg("Missing socket and/or directory");
    }
    settings.socketname = argv[optind];
    settings.directory = argv[optind + 1];

    if (settings.verbose) {
        fprintf(stdout, "Serving directory %s on socket %s\n",
                settings.directory, settings.socketname);
    }

    /* initialize the zmq context */
    context = zmq_init(1);
    check((context != NULL), "Could not create Zmq context");

    /* install signal handler */
    (void)signal(SIGTERM, shutdown);
    (void)signal(SIGINT, shutdown);

    /* create the sockets */
    in_socket = zmq_socket (context, ZMQ_XREP);
    check((in_socket != NULL), "Could not create zmq socket");
    check((zmq_bind(in_socket, settings.socketname) == 0),
            "could not bind to socket %s", settings.socketname);

    /* Socket to talk to workers */
    worker_socket = zmq_socket (context, ZMQ_XREQ);
    check((worker_socket != NULL), "Could not create internal zmq worker socket");
    check((zmq_bind(worker_socket, WORKER_SOCKET) == 0),
            "could not bind to socket %s", WORKER_SOCKET);


    /* startup the worker threads */
    workers = calloc(sizeof(pthread_t), settings.n_worker_threads);
    check_mem(workers);
    int t = 0;
    while (t<settings.n_worker_threads) {
        pthread_create(&workers[t], NULL, worker_routine, NULL);
        t++;
    }

    /* connect the worker threads to the incomming socket */
    check((zmq_device(ZMQ_QUEUE, in_socket, worker_socket) == 0),
            "Could not set up queue between sockets");

    shutdown(SIGTERM);
    exit(exit_code); /* surpress compiler warnings */

error:

    exit_code = EXIT_FAILURE;
    shutdown(0);
    exit(exit_code); /* surpress compiler warnings */
}
