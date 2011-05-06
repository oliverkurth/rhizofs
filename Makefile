WAF=./waf

.PHONY: force

default: release

clean: force
	$(WAF) clean

distclean: force
	$(WAF) distclean

debug: force
	$(WAF) build_debug

release: force
	$(WAF) build_release

install: force
	$(WAF) install_release

install_debug: force
	$(WAF) install_debug

all: build debug


# DEVELOPMENT targets ###############################################

VALGRIND=valgrind
VALGRIND_OPTS=--leak-check=full --show-reachable=yes --track-origins=yes
TEST_MOUNTPOINT=/tmp/rhizo-mp
SPLINT=splint
CPPCHECK=cppcheck
SOCKET_NAME=tcp://0.0.0.0:11555

valgrind-fs: debug
	[ -d $(TEST_MOUNTPOINT) ] || mkdir $(TEST_MOUNTPOINT)
	$(VALGRIND) $(VALGRIND_OPTS) ./build/debug/rhizofs -f $(SOCKET_NAME) $(TEST_MOUNTPOINT)

run-fs: debug
	[ -d $(TEST_MOUNTPOINT) ] || mkdir $(TEST_MOUNTPOINT)
	./build/debug/rhizofs -f $(SOCKET_NAME) $(TEST_MOUNTPOINT)

run-fs: debug
	[ -d $(TEST_MOUNTPOINT) ] || mkdir $(TEST_MOUNTPOINT)
	./build/debug/rhizofs -f $(SOCKET_NAME) $(TEST_MOUNTPOINT)


valgrind-srv: debug
	$(VALGRIND) $(VALGRIND_OPTS) ./build/debug/rhizosrv $(SOCKET_NAME) .

splint:
	@# need to add the build directory for the generated protobuf-c code
	find src/ -name '*.c' -o -name '*.h' | xargs $(SPLINT) \
		-I src \
		-I build/debug/src \
		-posix-strict-lib

cppcheck:
	$(CPPCHECK) -q --enable=all \
		-I src -I build/debug/src \
		src build/debug/src
