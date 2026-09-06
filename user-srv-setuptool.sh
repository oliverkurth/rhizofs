#!/bin/bash
set -e

# optional: directory to share instead of the home directory
# a path not starting with "/" is taken relative to the home directory
if [ -n "$1" ] ; then
    case "$1" in
        /*) SHARE_DIR="$1"; CHECK_DIR="$1" ;;
        *) SHARE_DIR="%h/$1"; CHECK_DIR="${HOME}/$1" ;;
    esac
    if [ ! -d "${CHECK_DIR}" ] ; then
        echo "directory ${CHECK_DIR} does not exist"
        exit 1
    fi
else
    SHARE_DIR="%h"
fi

cd "${HOME}"
if [ -f .config/rhizosrv/key ] ; then
	echo ".config/rhizosrv/key already exists - not overwriting"
else
	mkdir -p .config/rhizosrv/
	echo "generating .config/rhizosrv/key"
	rhizo-keygen .config/rhizosrv/key
fi

if [ -f .config/systemd/user/rhizosrv.service ] ; then
	echo ".config/systemd/user/rhizosrv.service exists - not creating service file"
else
	echo "creating systemd unit file .config/systemd/user/rhizosrv.service"
	mkdir -p .config/systemd/user/
	cat <<EOF > .config/systemd/user/rhizosrv.service
[Unit]
Description=RhizoFS server

[Service]
ExecStart=/usr/bin/rhizosrv -f -e --keyfile=%h/.config/rhizosrv/key --authorized-keys-file=%h/.config/rhizosrv/authorized_keys tcp://0.0.0.0:5555 ${SHARE_DIR}

[Install]
WantedBy=default.target
EOF
	echo "enabling and starting rhizosrv"
	systemctl --user daemon-reload
	systemctl --user enable rhizosrv.service
	systemctl --user start rhizosrv.service
fi
