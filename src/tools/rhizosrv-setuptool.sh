#!/bin/bash
set -e

if [ $# -lt 1 ] ; then
    echo "usage: $0 NAME [DIRECTORY] [PORT]"
    exit 1
fi

NAME=$1

# optional: directory to share instead of the home directory
# a path not starting with "/" is taken relative to the home directory
if [ -n "$2" ] ; then
    case "$2" in
        /*) SHARE_DIR="$2" ;;
        *) SHARE_DIR="${HOME}/$2" ;;
    esac
    if [ ! -d "${SHARE_DIR}" ] ; then
        echo "directory ${SHARE_DIR} does not exist"
        exit 1
    fi
else
    SHARE_DIR="${HOME}"
fi

PORT="${3:-5555}"

KEYFILE="${HOME}/.config/rhizosrv/${NAME}.key"
AUTHORIZED_KEYS_FILE="${HOME}/.config/rhizosrv/${NAME}.authorized_keys"
ENV_FILE="${HOME}/.config/rhizosrv/${NAME}.env"

cd "${HOME}"

mkdir -p .config/rhizosrv/
if [ -f "${KEYFILE}" ] ; then
    echo "${KEYFILE} already exists - not overwriting"
else
    echo "generating ${KEYFILE}"
    rhizo-keygen "${KEYFILE}"
fi

if [ -f "${ENV_FILE}" ] ; then
    echo "${ENV_FILE} exists - not overwriting"
else
    echo "creating ${ENV_FILE}"
    cat <<EOF > "${ENV_FILE}"
KEYFILE=${KEYFILE}
AUTHORIZED_KEYS_FILE=${AUTHORIZED_KEYS_FILE}
LISTEN_URL=tcp://0.0.0.0:${PORT}
SHARE_DIR=${SHARE_DIR}
EOF
fi

echo "enabling and starting rhizosrv@${NAME}"
systemctl --user daemon-reload
systemctl --user enable --now "rhizosrv@${NAME}.service"
