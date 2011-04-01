CFLAGS=-g -Wall -Isrc -I.
LIBS=-lzmq -lprotobuf-c
CC=clang

# tools
PROTOCC=protoc-c

# input files
SERVER_SOURCES=$(wildcard src/server/*.c src/*.c) src/proto/rhizofs.pb-c.c
SERVER_OBJECTS=$(patsubst %.c,%.o,${SERVER_SOURCES})

all: build proto bin/rhizofs_server

build:
	@mkdir bin

bin/rhizofs_server: ${SERVER_OBJECTS}
	$(CC) $(CFLAGS) $(LIBS) -o bin/rhizofs_server ${SERVER_OBJECTS}

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


proto: src/proto/rhizofs.pb-c.c

src/proto/rhizofs.pb-c.c:
	$(PROTOCC) --c_out=./ src/proto/rhizofs.proto

clean:
	rm -f src/proto/*.c
	rm -f src/proto/*.h
	rm -rf bin
	rm -f ${SERVER_OBJECTS}
