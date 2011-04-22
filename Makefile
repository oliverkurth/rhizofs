CFLAGS=-g -Wall -Isrc -I. `pkg-config fuse --cflags` -O2
LIBS=-lzmq -lprotobuf-c -lpthread
FUSE_LIBS=`pkg-config fuse --libs`

# tools
PROTOCC=protoc-c
PROTOC=protoc
#CC=clang

# input files
SERVER_SOURCES=$(wildcard src/util/*.c src/server/*.c src/*.c) src/proto/rhizofs.pb-c.c
SERVER_OBJECTS=$(patsubst %.c,%.o,${SERVER_SOURCES})
FS_SOURCES=$(wildcard src/util/*.c src/fs/*.c src/*.c) src/proto/rhizofs.pb-c.c
FS_OBJECTS=$(patsubst %.c,%.o,${FS_SOURCES})


all: build proto bin/rhizosrv testtool

dev: CFLAGS+=-Wextra -DDEBUG -O0
dev: all

build:
	@[ -d bin ] || mkdir bin

bin/rhizosrv: ${SERVER_OBJECTS}
	$(CC) $(CFLAGS) $(LIBS) -o bin/rhizosrv ${SERVER_OBJECTS}

bin/rhizofs: ${FS_OBJECTS}
	$(CC) $(CFLAGS) $(LIBS) $(FUSE_LIBS) -o bin/rhizofs ${FS_OBJECTS}

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


proto: src/proto/rhizofs.pb-c.c

src/proto/rhizofs.pb-c.c:
	$(PROTOCC) --c_out=./ src/proto/rhizofs.proto

clean:
	rm -f src/proto/*.c src/proto/*.h ${SERVER_OBJECTS} ${FS_OBJECTS}
	rm -rf bin
	rm -f testtool/rhizofs_pb.py

.PHONY: testtool src/proto/rhizofs.pb-c.c

testtool:
	$(PROTOC) --python_out=./testtool src/proto/rhizofs.proto
	mv ./testtool/src/proto/* ./testtool
	rmdir ./testtool/src/proto ./testtool/src

valgrind-srv: dev bin/rhizosrv
	valgrind   --leak-check=full --track-origins=yes ./bin/rhizosrv tcp://0.0.0.0:11555 /tmp/
