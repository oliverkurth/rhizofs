#ifndef __helptext_h__
#define __helptext_h__

#define HELPTEXT_INTRO \
"rhizofs and rhizosrv implement a filesytem which allows mounting\n" \
"remote directories on the local computer.\n"


#define HELPTEXT_SOCKET \
"Socket\n" \
"------\n" \
"   Socket specification as understood by ZeroMQ.\n" \
"\n" \
"   It is possible to specify all socket types supported\n" \
"   by zeromq, although socket types like inproc (local in-process\n" \
"   communication) make very little sense for this application.\n" \
"\n" \
"   Examples:\n" \
"\n" \
"   - TCP socket:       tcp://[host]:[port]\n" \
"   - UNIX socket:      ipc://[socket file]\n" \
"\n" \
"   For a complete list of available socket types see the man page for\n" \
"   'zmq_connect' or consult the zeromq website at http://zeromq.org.\n"


#define HELPTEXT_LOGGING \
"Logging\n" \
"=======\n" \
"   In the case of errors or warnings this program will log to syslog.\n"


#endif
