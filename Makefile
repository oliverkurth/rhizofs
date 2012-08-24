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
PROTOC=protoc
#CC=clang

# input files
SERVER_SOURCES=$(wildcard src/kazlib/*.c src/server/*.c src/*.c) src/proto/rhizofs.pb-c.c
SERVER_OBJECTS=$(patsubst %.c,%.o,${SERVER_SOURCES})
FS_SOURCES=$(wildcard src/kazlib/*.c src/fs/*.c src/*.c) src/proto/rhizofs.pb-c.c
FS_OBJECTS=$(patsubst %.c,%.o,${FS_SOURCES})


release: CFLAGS+=-DNDEBUG -O2
release: all

dev: CFLAGS+=-DDEBUG -O0 -g
dev: all

all: build proto bin/rhizosrv bin/rhizofs

build:
	@[ -d bin ] || mkdir bin

bin/rhizosrv: ${SERVER_OBJECTS} build
	$(CC) -o bin/rhizosrv ${SERVER_OBJECTS} $(CFLAGS) $(LIBS)

bin/rhizofs: ${FS_OBJECTS} build
	$(CC) -o bin/rhizofs ${FS_OBJECTS} $(CFLAGS) $(LIBS) $(FUSE_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -c $< -o $@


proto: src/proto/rhizofs.pb-c.c

src/proto/rhizofs.pb-c.c:
	$(PROTOCC) --c_out=./ src/proto/rhizofs.proto

clean:
	rm -f src/proto/*.c src/proto/*.h ${SERVER_OBJECTS} ${FS_OBJECTS}
	rm -rf bin

.PHONY: src/proto/rhizofs.pb-c.c build

valgrind-srv: dev bin/rhizosrv
	valgrind   --leak-check=full --track-origins=yes ./bin/rhizosrv tcp://0.0.0.0:11555 /tmp/

deb:
	dpkg-buildpackage
