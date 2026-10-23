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

if [ $# -lt 1 ] ; then
    echo "usage: $0 [--start] [--enable] NAME [DIRECTORY] [PORT]"
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
SERVICE="rhizosrv@${NAME}.service"

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
