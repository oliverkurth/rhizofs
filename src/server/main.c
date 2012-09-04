#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
#include <unistd.h>

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
    {"logfile", 1, 0, 'l'},
    {"pidfile", 1, 0, 'p'},
    {"foreground", 0, 0, 'f'},
    {0, 0, 0, 0}
};

static const char *opts_short = "hvn:Vl:fp:";

static const char *opts_desc =
    "  -n --numworkers=NUMBER  Number of worker threads to start [default=5]\n"
    "  -h --help\n"
    "  -v --version\n"
    "  -V --verbose\n"
    "  -l --logfile=FILE    logfile to use. It will always also be logged to syslog.\n"
    "  -p --pidfile=FILE    PID-file to write the PID of the daemonized server process to.\n"
    "  -f --foreground     foreground operation - do not daemonize.\n";


typedef struct ServerSettings {
    char * directory;
    char * socketname;
    int n_worker_threads;
    bool verbose;
    bool foreground; // foreground operation - do not daemonize
} ServerSettings;
static ServerSettings settings;


static void * context = NULL; // zmq context
static void * in_socket = NULL;
static void * worker_socket = NULL;
static int exit_code = EXIT_SUCCESS;
static pthread_t * workers = NULL;
static FILE * logfile = NULL;
static FILE * pidfile = NULL;


/* prototypes */
void print_wrong_arg(const char *);
void print_version();
void print_usage(const char *);
void shutdown(int);
void startup();
void daemonize();
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
startup()
{
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

    if (logfile) {
        fclose(logfile);
        logfile = NULL;
    }

    // the pidfile should be closed anyway. this is
    // only locatd here in case of an early exit of the
    // program
    if (pidfile) {
        fclose(pidfile);
        pidfile = NULL;
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

    ServeDir_serve(sd);

    ServeDir_destroy(sd);
    pthread_exit(NULL);

error:
    ServeDir_destroy(sd);
    pthread_exit(NULL);
}


void
daemonize()
{
     // daemonize the process
    pid_t pid;
    pid_t sid;

    pid = fork();
    if (pid < 0) {
        log_err("Unable to fork");
        exit_code = EXIT_FAILURE;
        shutdown(0);
    }

    if (pid > 0) {
        // exit the parent process
        exit(EXIT_SUCCESS);
    }

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        log_err("Unable to get SID for child process");
        exit_code = EXIT_FAILURE;
        shutdown(0);
    }

    // the PID file if desired
    if (pidfile) {
        fprintf(pidfile, "%d", (int)sid);
        fclose(pidfile);
        pidfile = NULL;
    }

    // close open files
    fclose(stdin);
    fclose(stderr);
    fclose(stdout);
}


int
main(int argc, char *argv[])
{
    int optc;
    const char * progname = argv[0];

    // logging configuration
    dbg_disable_logfile();
    dbg_enable_syslog();

    /* defaults */
    settings.n_worker_threads = DEFAULT_N_WORKER_THREADS;
    settings.verbose = false;
    settings.foreground = false;

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

            case 'l':
                logfile = fopen(optarg, "a");
                if (!logfile) {
                    fprintf(stderr, "Could not open logfile %s for writting: %s\n",
                        optarg, strerror(errno));
                    shutdown(0);
                }
                else {
                    dbg_set_logfile(logfile);
                }
                break;

            case 'p':
                pidfile = fopen(optarg, "w");
                if (!pidfile) {
                    fprintf(stderr, "Could not open pidfile %s for writting: %s\n",
                        optarg, strerror(errno));
                    shutdown(0);
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

            case 'f':
                settings.foreground = true;
                break;

            default:
                print_wrong_arg("Unknown option");
                break;
        }
    }

    if (argc < (optind + 2)) {
        print_wrong_arg("Missing socket and/or directory");
    }
    settings.socketname = argv[optind];
    settings.directory = argv[optind + 1];

    if (settings.verbose) {
        fprintf(stdout, "Serving directory %s on socket %s\n",
                settings.directory, settings.socketname);
    }

    if (!settings.foreground) {
        daemonize();
    }
    else {
        // output log messages to stderr when no
        // other logfile is specified and the process
        // runs in foreground
        if (!logfile) {
            dbg_set_logfile(stderr);
        }
    }

    startup();
}


