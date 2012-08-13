#ifndef __fs_rhizofs_h__
#define __fs_rhizofs_h__

#define INTERNAL_SOCKET_NAME "inproc://fuse"

/* response timeout after which interrupts will be checked */
#define POLL_TIMEOUT_USEC 10000

/* default response timeout (in seconds). see RhizoSettings struct */
#define RESPONSE_TIMEOUT_DEFAULT 30

#define SEND_SLEEP_USEC 1

int Rhizofs_run(int argc, char * argv[]);

#endif /* __fs_rhizofs_h__ */
