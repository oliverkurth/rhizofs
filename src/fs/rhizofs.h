#ifndef __fs_rhizofs_h__
#define __fs_rhizofs_h__

#define INTERNAL_SOCKET_NAME "inproc://fuse"

/* response timeout after which interrupts will be checked */
#define POLL_TIMEOUT_MSEC 1000

/* default timeout (in seconds). see RhizoSettings struct */
#define TIMEOUT_DEFAULT 30

#define SEND_SLEEP_USEC 1

#define ATTRCACHE_MAXSIZE 1000
#define ATTRCACHE_DEFAULT_MAXAGE_SEC 3

int Rhizofs_run(int argc, char * argv[]);

#endif /* __fs_rhizofs_h__ */
