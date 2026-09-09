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
MOUNT_POINT="${HOME}/${MNT_POINT}"
ENV_FILE="${HOME}/.config/rhizofs/${NAME}.env"

if [ ! -d "${MOUNT_POINT}" ] ; then
    echo "mount point ${MOUNT_POINT} does not exist"
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

if [ -f "${ENV_FILE}" ] ; then
    echo "${ENV_FILE} exists - not overwriting"
    exit 3
fi

echo "creating ${ENV_FILE}"
mkdir -p "$(dirname "${ENV_FILE}")"
cat <<EOF > "${ENV_FILE}"
CLIENT_KEYFILE=${CLIENT_KEYFILE}
SERVER_KEYFILE=${SERVER_KEYFILE}
SERVER_URL=${URL}
MOUNT_POINT=${MOUNT_POINT}
EOF

echo "enabling and starting rhizofs@${NAME}"
systemctl --user daemon-reload
systemctl --user enable --now "rhizofs@${NAME}.service"
