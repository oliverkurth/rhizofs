#!/bin/bash
set -e

START=0
ENABLE=0
POSITIONAL=()
for arg in "$@" ; do
    case "$arg" in
        --start) START=1 ;;
        --enable) ENABLE=1 ;;
        *) POSITIONAL+=("$arg") ;;
    esac
done
set -- "${POSITIONAL[@]}"

if [ $# -ne 3 ] ; then
    echo "usage: $0 [--start] [--enable] <name> <url> <mount-point-relative-to-home>"
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
SERVICE="rhizofs@${NAME}.service"

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
else
    echo "creating ${ENV_FILE}"
    mkdir -p "$(dirname "${ENV_FILE}")"
    cat <<EOF > "${ENV_FILE}"
CLIENT_KEYFILE=${CLIENT_KEYFILE}
SERVER_KEYFILE=${SERVER_KEYFILE}
SERVER_URL=${URL}
MOUNT_POINT=${MOUNT_POINT}
EOF
fi

systemctl --user daemon-reload

if [ "${ENABLE}" -eq 1 ] ; then
    echo "enabling ${SERVICE}"
    systemctl --user enable "${SERVICE}"
fi

if [ "${START}" -eq 1 ] ; then
    echo "restarting ${SERVICE}"
    systemctl --user restart "${SERVICE}"
fi

if [ "${ENABLE}" -eq 0 ] && [ "${START}" -eq 0 ] ; then
    cat <<MSG
${SERVICE} was not enabled or started. To do so:
    systemctl --user daemon-reload
    systemctl --user enable ${SERVICE}
    systemctl --user start ${SERVICE}
MSG
fi
