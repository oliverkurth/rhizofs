#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zmq.h>

#include "../dbg.h"
#include "../version.h"
#include "../helptext.h"
#include "servedir.h"

#define DEFAULT_N_WORKER_THREADS 5
#define MAX_N_WORKER_THREADS 200
#define WORKER_SOCKET "inproc://workers"


struct option opts_long[] = {
    {"authorized-keys-file", 1, 0, 'a'},
    {"encrypt",    0, 0, 'e'},
    {"foreground", 0, 0, 'f'},
    {"help",       0, 0, 'h'},
    {"keyfile",    1, 0, 'k'},
    {"logfile",    1, 0, 'l'},
    {"numworkers", 1, 0, 'n'},
    {"pidfile",    1, 0, 'p'},
    {"pubkeyfile", 1, 0, 'P'},
    {"version",    0, 0, 'v'},
    {"verbose",    0, 0, 'V'},
    {0, 0, 0, 0}
};


static const char *opts_short = "a:ehk:vn:Vl:fp:P:";


static const char *opts_desc =
    "  -a --authorized-keys-file authorized keys file.\n"
    "  -e --encrypt\n"
    "  -f --foreground           foreground operation - do not daemonize.\n"
    "  -h --help\n"
    "  -k --keyfile=FILE         File to read for the public key. The secret key\n"
    "                            will be read from the file with the same name but\n"
    "                            with '.secret' appended.\n"
    "  -l --logfile=FILE         Logfile to use. Additionally it will always\n"
    "                            be logged to the syslog.\n"
    "  -n --numworkers=NUMBER    Number of worker threads to start [default=5]\n"
    "  -p --pidfile=FILE         PID-file to write the PID of the daemonized server\n"
    "                            process to.\n"
    "                            Has no effect if the server runs in the foreground.\n"
    "  -P --pubkeyfile           File to store the public key (needs --encrypt).\n"
    "                            If not set, the public key will be written to stdout.\n"
    "  -V --verbose\n"
    "  -v --version\n";


typedef struct ServerSettings {
    char * directory;
    char * socketname;
    int n_worker_threads;
    bool encrypt;
    bool foreground; // foreground operation - do not daemonize
    bool verbose;
    char *authorized_keys_file;
} ServerSettings;
static ServerSettings settings;


static void * context = NULL; // zmq context
static void * in_socket = NULL;
static void * worker_socket = NULL;
static int exit_code = EXIT_SUCCESS;
static pthread_t *workers = NULL;
static pthread_t auth_thread = 0;
static FILE * logfile = NULL;
static FILE * pidfile = NULL;


/* prototypes */
void print_wrong_arg(const char *);
void print_version();
void print_usage(const char *);
void shutdown(int);
void startup(const char *secret_key);
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
        "%s SOCKET DIRECTORY [options]\n"
        "\n"
        HELPTEXT_INTRO
        "\n"
        "This program implements the server.\n"
        "\n"
        "Parameters\n"
        "==========\n"
        "\n"
        "The parameters SOCKET and MOUNTPOINT are mandatory.\n"
        "\n"
        HELPTEXT_SOCKET
        "\n"
        "Directory\n"
        "---------\n"
        "   The directory to be shared.\n"
        "\n"
        "Options\n"
        "-------\n"
        "%s\n"
        HELPTEXT_LOGGING
        "\n", progname, opts_desc
    );
}

static
void send_string(void* socket, const char *message, const bool send_more)
{
    zmq_send(socket, message, strlen(message), send_more ? ZMQ_SNDMORE : 0);
}

static
char *receive_string(void* socket)
{
    const int timeout_in_ms = -1;
    zmq_pollitem_t poll_items[] = {{ socket, 0, ZMQ_POLLIN, 0 }};
    check((zmq_poll(poll_items, 1, timeout_in_ms) >= 0), "zmq_poll() failed");

    if (poll_items[0].revents & ZMQ_POLLIN) {
        char buf[1024];
        int rc = zmq_recv(socket, buf, 1024, 0);
        check((rc >= 0), "zmq_recv() failed: %s (%d)", strerror(errno), errno);
        buf[rc] = 0;
        return strdup(buf);
    }
error:
    return NULL;
}

static
char *receive_message(void* socket, size_t size)
{
    const int timeout_in_ms = -1;
    zmq_pollitem_t poll_items[] = {{ socket, 0, ZMQ_POLLIN, 0 }};
    check((zmq_poll(poll_items, 1, timeout_in_ms) >= 0), "zmq_poll() failed");

    if (poll_items[0].revents & ZMQ_POLLIN) {
        char buf[size];
        int rc = zmq_recv(socket, buf, size, 0);
        check((rc >= 0), "zmq_recv() failed: %s (%d)", strerror(errno), errno);
        check((rc == (int)size), "unexpected size %d from zmq_recv(), expected %d", rc, (int)size)
        char *ptr = (char *)malloc(size);
        memcpy(ptr, buf, size);
        return ptr;
    }
error:
    return NULL;
}

static volatile bool authentication_ready_flag = false;

void *auth_routine(void* ctx)
{
    void* sock = zmq_socket(ctx, ZMQ_REP);
    check((sock != NULL), "could not create socket");

    check((zmq_bind(sock, "inproc://zeromq.zap.01") == 0),
          "could not bind to zap socket");
    authentication_ready_flag = true;

    while (true)
    {
        char *status_code = "400";
        char *status_msg = "denied";

        char *version = receive_string(sock);
        char *request_id = receive_string(sock);
        char *domain = receive_string(sock);
        char *address = receive_string(sock);
        char *identity_property = receive_string(sock);
        char *mechanism = receive_string(sock);

        char *client_key = NULL;
        char client_key_text[41];
        if (strcmp(mechanism, "CURVE") == 0) {
            client_key = receive_message(sock, 32);
            if (client_key == NULL) {
                log_err("could not receive client key");
                status_code = "300";
                status_msg = "internal error";
                goto out;
            }
            zmq_z85_encode(client_key_text, (uint8_t *)client_key, 32);
        }

        if (strcmp(version, "1.0") != 0) {
            log_err("invalid ZAP version received");
            status_code = "300";
            status_msg = "invalid ZAP version";
        } else if ((strcmp(mechanism, "CURVE") == 0) &&
                   (strlen(client_key_text) == 40)) {
            FILE *fptr = fopen(settings.authorized_keys_file, "rt");
            bool found = false;
            char buf[41];
            if (fptr != NULL) {
                while (fgets(buf, 41, fptr) != NULL) {
                    if (strcmp(buf, client_key_text) == 0) {
                        found = true;
                        break;
                    }
                }
                fclose(fptr);
                if (found) {
                    log_info("key from %s accepted", address);
                    status_code = "200";
                    status_msg = "OK";
                } else {
                    log_warn("request from %s key '%s' not authorized", address, client_key_text);
                }
            } else {
                log_err("could not open authorized keys file %s: %s (%d)",
                        settings.authorized_keys_file, strerror(errno), errno);
                status_code = "300";
                status_msg = "internal error";
            }
        }

out:
        send_string(sock, "1.0", true);
        send_string(sock, request_id, true);
        send_string(sock, status_code, true);
        send_string(sock, status_msg, true);
        send_string(sock, "", true);
        send_string(sock, "", false);

        free(version);
        free(domain);
        free(request_id);
        free(identity_property);
        free(address);
        free(mechanism);
        free(client_key);
    }

error:
    zmq_close(sock);
    pthread_exit(NULL);
}

void
startup(const char *secret_key)
{
    /* initialize the zmq context */
    context = zmq_ctx_new();
    check((context != NULL), "Could not create Zmq context");

    /* install signal handler */
    (void)signal(SIGTERM, shutdown);
    (void)signal(SIGINT, shutdown);

    if (settings.authorized_keys_file != NULL) {
        pthread_create(&auth_thread, NULL, auth_routine, context);
        useconds_t timeout;
        for (timeout = 1000000; timeout > 0; timeout -= 1000) {
            if (authentication_ready_flag)
                break;
            usleep(1000);
        }
        check((authentication_ready_flag), "auth thread did not start");
    }

    /* create the sockets */
    in_socket = zmq_socket (context, ZMQ_XREP);
    check((in_socket != NULL), "Could not create zmq socket");

    if (secret_key != NULL) {
        const int curve_server_enable = 1;
        zmq_setsockopt(in_socket, ZMQ_CURVE_SERVER, &curve_server_enable, sizeof(curve_server_enable));
        zmq_setsockopt(in_socket, ZMQ_CURVE_SECRETKEY, secret_key, 40);
    }

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
    while (t < settings.n_worker_threads) {
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
    exit(exit_code); /* suppress compiler warnings */
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

    if (auth_thread != 0) {
        pthread_join(auth_thread, NULL);
    }

    if (logfile) {
        fclose(logfile);
        logfile = NULL;
    }

    // the pidfile should be closed anyway. this is
    // only located here in case of an early exit of the
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
        fprintf(pidfile, "%d\n", (int)sid);
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
    int exit_code = 0;
    const char * progname = argv[0];
    char public_key[41];
    char secret_key[41];
    char *key_file = NULL; /* file to read for keys */
    char *pubkey_file = NULL;   /* file to write the public key */

    // logging configuration
    dbg_disable_logfile();
    dbg_enable_syslog();

    /* defaults */
    settings.n_worker_threads = DEFAULT_N_WORKER_THREADS;
    settings.verbose = false;
    settings.foreground = false;

    while ((optc = getopt_long(argc, argv, opts_short, opts_long, NULL)) != -1) {

        switch (optc) {
            case 'a':
                settings.authorized_keys_file = strdup(optarg);
                break;
            case 'n':
                settings.n_worker_threads = atoi(optarg);
                if ((settings.n_worker_threads < 1)
                        || (settings.n_worker_threads > MAX_N_WORKER_THREADS))
                    {
                    print_wrong_arg("Illegal value for numworkers");
                }
                break;
            case 'k':
                key_file = strdup(optarg);
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
                    fprintf(stderr, "Could not open pidfile %s for writting: %s (%d)\n",
                        optarg, strerror(errno), errno);
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

            case 'e':
                settings.encrypt = true;
                break;

            case 'f':
                settings.foreground = true;
                break;

            case 'P':
                pubkey_file = strdup(optarg);
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

    if (settings.encrypt) {
        /* if a keyfile is set, use that. Otherwise generate keypair on the fly */
        if (key_file) {
            FILE *fptr = NULL;
            char secret_key_file[PATH_MAX];

            fptr = fopen(key_file, "rt");
            check(fptr, "could not open %s", key_file);
            check((fread(public_key, 1, 40, fptr) == 40),
                "could not read %s", key_file);
            fclose(fptr);

            snprintf(secret_key_file, sizeof(secret_key_file),
                     "%s.secret", key_file);

            fptr = fopen(secret_key_file, "rt");
            check(fptr, "could not open %s", secret_key_file);
            check((fread(secret_key, 1, 40, fptr) == 40),
                "could not read %s", secret_key_file);
            fclose(fptr);
        } else {
            check(zmq_curve_keypair(public_key, secret_key) == 0,
                  "could not create key pair");
            if (pubkey_file) {
                 /* no rw for group and others */
                mode_t old_mode = umask(0066);
                FILE *fptr = fopen(pubkey_file, "wt");
                check(fptr, "could not open public key file %s for writing", pubkey_file);
                fprintf(fptr, "%s", public_key);
                fclose(fptr);
                umask(old_mode);
            } else {
                printf("public key: %s\n", public_key);
            }
        }
    }

    if (!settings.foreground) {
        daemonize();
    } else {
        // output log messages to stderr when no
        // other logfile is specified and the process
        // runs in foreground
        if (!logfile) {
            dbg_set_logfile(stderr);
        }
    }

    startup(settings.encrypt ? secret_key : NULL);

cleanup:
    if (pubkey_file)
        free(pubkey_file);
    if (key_file)
        free(key_file);
    exit(exit_code);

error:
   exit_code = 1;
   goto cleanup;
}
