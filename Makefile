UNAME_S := $(shell uname -s)

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

# on macOS, _XOPEN_SOURCE hides the BSD u_int/u_short/u_long/u_char
# typedefs (needed by system headers pulled in via fuse3) unless
# _DARWIN_C_SOURCE is also defined
ifeq (${UNAME_S},Darwin)
CFLAGS+=-D_DARWIN_C_SOURCE
endif

# clang emits a warning if the -std flag is passed to it when linking
# objects
CFLAGS_EXTRA = -std=c99

LIBS=$(shell pkg-config libzmq --libs) $(shell pkg-config libprotobuf-c --libs) -lpthread
FUSE_LIBS=$(shell pkg-config fuse3 --libs)

# tools
PROTOCC=protoc-c
#CC=clang

PREFIX?=/usr/local
USERUNITDIR?=$(PREFIX)/lib/systemd/user
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
RAWCLIENT_SOURCES=tests/rawclient.c
RAWCLIENT_OBJECTS=$(patsubst %.c,%.o,${RAWCLIENT_SOURCES})
SECURITYUNIT_SOURCES=tests/security_unit.c
SECURITYUNIT_OBJECTS=$(patsubst %.c,%.o,${SECURITYUNIT_SOURCES})
RAWCLIENT_COMMON_OBJECTS=src/kazlib/hash.o src/datablock.o src/dbg.o src/hashfunc.o \
	src/lz4.o src/mapping.o src/path.o src/posix.o src/request.o src/response.o \
	src/proto/rhizofs.pb-c.o

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

# test-only tools used by the pytest regression tests: rhizo-rawclient
# talks to the wire protocol directly, rhizo-security-unit exercises the
# checks that are not reachable from the outside. not part of
# "all"/"install".
testtools: ${BINDIR}/rhizo-rawclient ${BINDIR}/rhizo-security-unit

# kept as an alias for the previous name of this target
testclient: testtools

${BINDIR}/rhizo-rawclient: ${RAWCLIENT_OBJECTS} ${RAWCLIENT_COMMON_OBJECTS} ${BINDIR}
	$(CC) -o ${BINDIR}/rhizo-rawclient ${RAWCLIENT_OBJECTS} ${RAWCLIENT_COMMON_OBJECTS} $(LIBS)

${BINDIR}/rhizo-security-unit: ${SECURITYUNIT_OBJECTS} ${RAWCLIENT_COMMON_OBJECTS} ${BINDIR}
	$(CC) -o ${BINDIR}/rhizo-security-unit ${SECURITYUNIT_OBJECTS} ${RAWCLIENT_COMMON_OBJECTS} $(LIBS)

${SERVER_SOURCES} ${FS_SOURCES} ${RAWCLIENT_SOURCES} ${SECURITYUNIT_SOURCES}: ${PROTO_H_COMPILED}

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -c $< -o $@

%.pb-c.c: %.proto
	$(PROTOCC) --c_out=./ $<

%.pb-c.h: %.proto
	$(PROTOCC) --c_out=./ $<

clean:
	rm -f ${SERVER_OBJECTS} ${FS_OBJECTS} ${TOOLS_OBJECTS} ${RAWCLIENT_OBJECTS} ${SECURITYUNIT_OBJECTS} ${PROTO_C_COMPILED} ${PROTO_H_COMPILED}
	rm -rf ${BINDIR}

valgrind-srv: dev ${BINDIR}/rhizosrv
	valgrind   --leak-check=full --track-origins=yes ${BINDIR}/rhizosrv tcp://0.0.0.0:11555 /tmp/

deb:
	./scripts/build-deb.sh

deb-clean:
	fakeroot debian/rules clean
	rm -f ../rhizofs_*.deb ../rhizofs-*_*.deb ../rhizofs_*.changes ../rhizofs_*.buildinfo ../rhizofs_*.dsc ../rhizofs_*.tar.* ../rhizofs_*.build

install: release
	install ${BINDIR}/rhizosrv $(PREFIX)/bin/
	install ${BINDIR}/rhizofs $(PREFIX)/bin/
	install ${BINDIR}/rhizo-keygen $(PREFIX)/bin/
	install src/tools/rhizofs-setuptool.sh $(PREFIX)/bin/
	install src/tools/rhizosrv-setuptool.sh $(PREFIX)/bin/
	install -d $(USERUNITDIR)
	install -m 644 systemd/rhizofs@.service $(USERUNITDIR)/
	install -m 644 systemd/rhizosrv@.service $(USERUNITDIR)/

