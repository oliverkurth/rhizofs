THIS_DIR := $(dir $(abspath $(firstword $(MAKEFILE_LIST))))

project_dir := $(abspath $(THIS_DIR)/..)

outdir ?= $(project_dir)
name := rhizofs
spec_file := $(project_dir)/$(name).spec
version := 0.2.7
tarball := $(name)-$(version).tar.gz

prereq:
	which git || dnf -y install git

spec:

srpm: spec prereq
	cd $(project_dir) && \
		git archive --format=tar.gz -o $(tarball) --prefix=$(name)-$(version)/ HEAD && \
		rpmbuild -D'_srcrpmdir $(outdir)' -D'_sourcedir $(project_dir)' -bs $(spec_file)
