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
LABEL="com.rhizofs.server.${NAME}"
LOG_DIR="${HOME}/Library/Logs/rhizosrv"
PLIST="${HOME}/Library/LaunchAgents/${LABEL}.plist"
TARGET="gui/$(id -u)/${LABEL}"

# resolved relative to this script's install location (PREFIX/bin), so it
# tracks whatever PREFIX the package was installed with
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEMPLATE="${SCRIPT_DIR}/../share/rhizofs/launchd/rhizosrv.plist.template"
RHIZOSRV_BIN="${SCRIPT_DIR}/rhizosrv"

if [ ! -f "${TEMPLATE}" ] ; then
    echo "template ${TEMPLATE} not found"
    exit 4
fi

mkdir -p "$(dirname "${KEYFILE}")"
if [ -f "${KEYFILE}" ] ; then
    echo "${KEYFILE} already exists - not overwriting"
else
    echo "generating ${KEYFILE}"
    rhizo-keygen "${KEYFILE}"
fi

if [ -f "${PLIST}" ] ; then
    echo "${PLIST} exists - not overwriting"
else
    mkdir -p "$(dirname "${PLIST}")" "${LOG_DIR}"

    echo "creating ${PLIST}"
    sed \
        -e "s#__LABEL__#${LABEL}#g" \
        -e "s#__RHIZOSRV_BIN__#${RHIZOSRV_BIN}#g" \
        -e "s#__KEYFILE__#${KEYFILE}#g" \
        -e "s#__AUTHORIZED_KEYS_FILE__#${AUTHORIZED_KEYS_FILE}#g" \
        -e "s#__LISTEN_URL__#tcp://0.0.0.0:${PORT}#g" \
        -e "s#__SHARE_DIR__#${SHARE_DIR}#g" \
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
