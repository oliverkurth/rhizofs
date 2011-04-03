CFLAGS=-g -Wall -Isrc -I.
LIBS=-lzmq -lprotobuf-c
CC=clang

# tools
PROTOCC=protoc-c

# input files
SERVER_SOURCES=$(wildcard src/server/*.c src/*.c) src/proto/rhizofs.pb-c.c
SERVER_OBJECTS=$(patsubst %.c,%.o,${SERVER_SOURCES})
FS_SOURCES=$(wildcard src/fs/*.c src/*.c) src/proto/rhizofs.pb-c.c
FS_OBJECTS=$(patsubst %.c,%.o,${FS_SOURCES})

dev: CFLAGS=-g -Wall -I. -Isrc -Wall -Wextra -DDEBUG
dev: all

all: build proto bin/rhizosrv

build:
	@[ -d bin ] || mkdir bin

bin/rhizosrv: ${SERVER_OBJECTS}
	$(CC) $(CFLAGS) $(LIBS) -o bin/rhizosrv ${SERVER_OBJECTS}

bin/rhizofs: ${FS_OBJECTS}
	$(CC) $(CFLAGS) $(LIBS) -o bin/rhizofs ${FS_OBJECTS}

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


proto: src/proto/rhizofs.pb-c.c

src/proto/rhizofs.pb-c.c:
	$(PROTOCC) --c_out=./ src/proto/rhizofs.proto

clean:
	rm -f src/proto/*.c src/proto/*.h \
		${SERVER_OBJECTS} ${FS_OBJECTS}
	rm -rf bin
