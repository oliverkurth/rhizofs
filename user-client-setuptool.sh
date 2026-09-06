#!/bin/bash
set -e

if [ $# -ne 3 ] ; then
    echo "usage: $0 <name> <url> <mount-point-relative-to-home>"
    exit 1
fi

NAME=$1
URL=$2
# relative to home dir
MNT_POINT=$3

CLIENT_KEYFILE="${HOME}/.config/rhizofs/key"
SERVER_KEYFILE="${HOME}/.config/rhizofs/${NAME}"
UNIT_FILE="${HOME}/.config/systemd/user/rhizofs-${NAME}.service"

if [ ! -d "${HOME}/${MNT_POINT}" ] ; then
    echo "mount point ${HOME}/${MNT_POINT} does not exist"
    exit 1
fi

if [ ! -f "${SERVER_KEYFILE}" ] ; then
    echo "server key file ${SERVER_KEYFILE} does not exist"
    exit 2
fi

if [ -f "${CLIENT_KEYFILE}" ] ; then
    echo "${CLIENT_KEYFILE} already exists - not overwriting"
else
    mkdir -p "$(dirname "${CLIENT_KEYFILE}")"
    echo "generating ${CLIENT_KEYFILE}"
    rhizo-keygen "${CLIENT_KEYFILE}"
fi

if [ -f "${UNIT_FILE}" ] ; then
    echo "${UNIT_FILE} exists - not creating service file"
    exit 3
fi

echo "creating systemd unit file ${UNIT_FILE}"
mkdir -p "$(dirname "${UNIT_FILE}")"
cat <<EOF > "${UNIT_FILE}"
[Unit]
Description=RhizoFS client for ${NAME}
After=network-online.target
Wants=network-online.target
StartLimitIntervalSec=300
StartLimitBurst=10

[Install]
WantedBy=default.target

[Service]
Type=exec
ExecStart=/usr/bin/rhizofs -f --clientpubkeyfile=${CLIENT_KEYFILE} --pubkeyfile=${SERVER_KEYFILE} ${URL} %h/${MNT_POINT}
ExecStop=/usr/bin/umount %h/${MNT_POINT}
Restart=on-failure
RestartSec=20
EOF

echo "enabling and starting rhizosrv"
systemctl --user daemon-reload
systemctl --user enable --now "$(basename "${UNIT_FILE}")"

