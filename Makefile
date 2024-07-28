CFLAGS= -Wall \
	-Wextra \
	-Wno-format-extra-args \
	-Wformat-nonliteral \
	-Wformat-security \
	-Wformat=2 \
	-Isrc \
	-D_XOPEN_SOURCE=600 \
	-D_DEFAULT_SOURCE \
	-I. $(shell pkg-config fuse3 --cflags) \
	-I. $(shell pkg-config libprotobuf-c --cflags) \
	-I. $(shell pkg-config libzmq --cflags)

# clang emits a warning if the -std flag is passed to it when linking
# objects
CFLAGS_EXTRA = -std=c99

LIBS=$(shell pkg-config libzmq --libs) $(shell pkg-config libprotobuf-c --libs) -lpthread
FUSE_LIBS=$(shell pkg-config fuse3 --libs)

# tools
PROTOCC=protoc-c
#CC=clang

PREFIX?=/usr/local
BINDIR=./bin

# input files
PROTO_SOURCES=$(wildcard src/proto/*.proto)
PROTO_C_COMPILED=$(patsubst %.proto,%.pb-c.c,${PROTO_SOURCES})
PROTO_H_COMPILED=$(patsubst %.proto,%.pb-c.h,${PROTO_SOURCES})
SERVER_SOURCES=$(wildcard src/kazlib/*.c src/server/*.c src/*.c) ${PROTO_C_COMPILED}
SERVER_OBJECTS=$(patsubst %.c,%.o,${SERVER_SOURCES})
FS_SOURCES=$(wildcard src/kazlib/*.c src/fs/*.c src/*.c) ${PROTO_C_COMPILED}
FS_OBJECTS=$(patsubst %.c,%.o,${FS_SOURCES})
TOOLS_SOURCES=$(wildcard src/tools/*.c)
TOOLS_OBJECTS=$(patsubst %.c,%.o,${TOOLS_SOURCES})

# do not strip debuging information in release builds
release: CFLAGS+=-DNDEBUG -O2 -g
release: all

dev: CFLAGS+=-DDEBUG -O0 -g
dev: all

all: ${BINDIR}/rhizosrv ${BINDIR}/rhizofs ${BINDIR}/rhizo-keygen

${BINDIR}:
	@[ -d ${BINDIR} ] || mkdir ${BINDIR}

${BINDIR}/rhizosrv: ${SERVER_OBJECTS} ${BINDIR}
	$(CC) -o ${BINDIR}/rhizosrv ${SERVER_OBJECTS} $(LIBS)

${BINDIR}/rhizofs: ${FS_OBJECTS} ${BINDIR}
	$(CC) -o ${BINDIR}/rhizofs ${FS_OBJECTS} $(LIBS) $(FUSE_LIBS)

${BINDIR}/rhizo-keygen: ${TOOLS_OBJECTS} ${BINDIR}
	$(CC) -o ${BINDIR}/rhizo-keygen ${TOOLS_OBJECTS} $(shell pkg-config libzmq --libs)

${SERVER_SOURCES} ${FS_SOURCES}: ${PROTO_H_COMPILED}

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -c $< -o $@

%.pb-c.c: %.proto
	$(PROTOCC) --c_out=./ $<

%.pb-c.h: %.proto
	$(PROTOCC) --c_out=./ $<

clean:
	rm -f ${SERVER_OBJECTS} ${FS_OBJECTS} ${TOOLS_OBJECTS} ${PROTO_C_COMPILED} ${PROTO_H_COMPILED}
	rm -rf ${BINDIR}

valgrind-srv: dev ${BINDIR}/rhizosrv
	valgrind   --leak-check=full --track-origins=yes ${BINDIR}/rhizosrv tcp://0.0.0.0:11555 /tmp/

deb:
	dpkg-buildpackage -uc -us

install: release
	install ${BINDIR}/rhizosrv $(PREFIX)/bin/
	install ${BINDIR}/rhizofs $(PREFIX)/bin/
	install ${BINDIR}/rhizo-keygen $(PREFIX)/bin/

