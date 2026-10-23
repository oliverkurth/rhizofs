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
LABEL="com.rhizofs.client.${NAME}"
LOG_DIR="${HOME}/Library/Logs/rhizofs"
PLIST="${HOME}/Library/LaunchAgents/${LABEL}.plist"
TARGET="gui/$(id -u)/${LABEL}"

# resolved relative to this script's install location (PREFIX/bin), so it
# tracks whatever PREFIX the package was installed with
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEMPLATE="${SCRIPT_DIR}/../share/rhizofs/launchd/rhizofs.plist.template"
RHIZOFS_BIN="${SCRIPT_DIR}/rhizofs"

if [ ! -f "${TEMPLATE}" ] ; then
    echo "template ${TEMPLATE} not found"
    exit 4
fi

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

if [ -f "${PLIST}" ] ; then
    echo "${PLIST} exists - not overwriting"
else
    mkdir -p "$(dirname "${PLIST}")" "${LOG_DIR}"

    echo "creating ${PLIST}"
    sed \
        -e "s#__LABEL__#${LABEL}#g" \
        -e "s#__RHIZOFS_BIN__#${RHIZOFS_BIN}#g" \
        -e "s#__CLIENT_KEYFILE__#${CLIENT_KEYFILE}#g" \
        -e "s#__SERVER_KEYFILE__#${SERVER_KEYFILE}#g" \
        -e "s#__SERVER_URL__#${URL}#g" \
        -e "s#__MOUNT_POINT__#${MOUNT_POINT}#g" \
        -e "s#__LOG_DIR__#${LOG_DIR}#g" \
        -e "s#__NAME__#${NAME}#g" \
        "${TEMPLATE}" > "${PLIST}"
fi

if [ "${ENABLE}" -eq 1 ] ; then
    echo "enabling ${LABEL}"
    launchctl enable "${TARGET}"
fi

if [ "${START}" -eq 1 ] ; then
    if launchctl print "${TARGET}" >/dev/null 2>&1 ; then
        echo "restarting ${LABEL}"
        launchctl kickstart -k "${TARGET}"
    else
        echo "loading ${LABEL}"
        launchctl bootstrap "gui/$(id -u)" "${PLIST}"
    fi
fi

if [ "${ENABLE}" -eq 0 ] && [ "${START}" -eq 0 ] ; then
    cat <<MSG
${LABEL} was not enabled or started. To do so:
    launchctl enable ${TARGET}
    launchctl bootstrap gui/$(id -u) ${PLIST}
MSG
fi
