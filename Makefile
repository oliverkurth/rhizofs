CFLAGS= -Wall \
	-Wextra \
	-Wno-format-extra-args \
	-Wformat-nonliteral \
	-Wformat-security \
	-Wformat=2 \
	-Isrc \
	-std=c99 \
	-D_XOPEN_SOURCE=500 \
	-D_BSD_SOURCE \
	-I. $(shell pkg-config fuse --cflags)
	#-D_BSD_SOURCE \

LIBS=-lzmq -lprotobuf-c -lpthread
FUSE_LIBS=$(shell pkg-config fuse --libs)

# tools
PROTOCC=protoc-c
PROTOC=protoc
#CC=clang

# input files
SERVER_SOURCES=$(wildcard src/util/*.c src/server/*.c src/*.c) src/proto/rhizofs.pb-c.c
SERVER_OBJECTS=$(patsubst %.c,%.o,${SERVER_SOURCES})
FS_SOURCES=$(wildcard src/util/*.c src/fs/*.c src/*.c) src/proto/rhizofs.pb-c.c
FS_OBJECTS=$(patsubst %.c,%.o,${FS_SOURCES})


release: CFLAGS+=-DNDEBUG -O2
release: all

dev: CFLAGS+=-DDEBUG -O0 -g
dev: all

all: build proto bin/rhizosrv bin/rhizofs testtool

build:
	@[ -d bin ] || mkdir bin

bin/rhizosrv: ${SERVER_OBJECTS} build
	$(CC) -o bin/rhizosrv ${SERVER_OBJECTS} $(CFLAGS) $(LIBS)

bin/rhizofs: ${FS_OBJECTS} build
	$(CC) -o bin/rhizofs ${FS_OBJECTS} $(CFLAGS) $(LIBS) $(FUSE_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


proto: src/proto/rhizofs.pb-c.c

src/proto/rhizofs.pb-c.c:
	$(PROTOCC) --c_out=./ src/proto/rhizofs.proto

clean:
	rm -f src/proto/*.c src/proto/*.h ${SERVER_OBJECTS} ${FS_OBJECTS}
	rm -rf bin
	rm -f testtool/rhizofs_pb.py

.PHONY: testtool src/proto/rhizofs.pb-c.c build

testtool:
	$(PROTOC) --python_out=./testtool src/proto/rhizofs.proto
	mv ./testtool/src/proto/* ./testtool
	rmdir ./testtool/src/proto ./testtool/src

valgrind-srv: dev bin/rhizosrv
	valgrind   --leak-check=full --track-origins=yes ./bin/rhizosrv tcp://0.0.0.0:11555 /tmp/
