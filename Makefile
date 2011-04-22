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

all: build build_debug

