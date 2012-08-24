CFLAGS= -Wall \
	-Wextra \
	-Wno-format-extra-args \
	-Wformat-nonliteral \
	-Wformat-security \
	-Wformat=2 \
	-Isrc \
	-D_XOPEN_SOURCE=500 \
	-I. $(shell pkg-config fuse --cflags)
	#-D_BSD_SOURCE \

# clang emits a warning if the -std flag is passed to it when linking
# objects
CFLAGS_EXTRA = -std=c99

LIBS=-lzmq -lprotobuf-c -lpthread
FUSE_LIBS=$(shell pkg-config fuse --libs)

# tools
PROTOCC=protoc-c
#CC=clang

BINDIR=./bin

# input files
PROTO_SOURCES=$(wildcard src/proto/*.proto)
PROTO_C_COMPILED=$(patsubst %.proto,%.pb-c.c,${PROTO_SOURCES})
PROTO_H_COMPILED=$(patsubst %.proto,%.pb-c.h,${PROTO_SOURCES})
SERVER_SOURCES=$(wildcard src/kazlib/*.c src/server/*.c src/*.c) ${PROTO_C_COMPILED}
SERVER_OBJECTS=$(patsubst %.c,%.o,${SERVER_SOURCES})
FS_SOURCES=$(wildcard src/kazlib/*.c src/fs/*.c src/*.c) ${PROTO_C_COMPILED}
FS_OBJECTS=$(patsubst %.c,%.o,${FS_SOURCES})


release: CFLAGS+=-DNDEBUG -O2
release: all

dev: CFLAGS+=-DDEBUG -O0 -g
dev: all

all: ${BINDIR}/rhizosrv ${BINDIR}/rhizofs

${BINDIR}:
	@[ -d ${BINDIR} ] || mkdir ${BINDIR}

${BINDIR}/rhizosrv: ${SERVER_OBJECTS} ${BINDIR}
	$(CC) -o ${BINDIR}/rhizosrv ${SERVER_OBJECTS} $(CFLAGS) $(LIBS)

${BINDIR}/rhizofs: ${FS_OBJECTS} ${BINDIR}
	$(CC) -o ${BINDIR}/rhizofs ${FS_OBJECTS} $(CFLAGS) $(LIBS) $(FUSE_LIBS)

${SERVER_SOURCES} ${FS_SOURCES}: ${PROTO_H_COMPILED}

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -c $< -o $@

%.pb-c.c: %.proto
	$(PROTOCC) --c_out=./ $<

%.pb-c.h: %.proto
	$(PROTOCC) --c_out=./ $<

clean:
	rm -f ${SERVER_OBJECTS} ${FS_OBJECTS} ${PROTO_C_COMPILED} ${PROTO_H_COMPILED}
	rm -rf ${BINDIR}

valgrind-srv: dev ${BINDIR}/rhizosrv
	valgrind   --leak-check=full --track-origins=yes ${BINDIR}/rhizosrv tcp://0.0.0.0:11555 /tmp/

deb:
	dpkg-buildpackage
