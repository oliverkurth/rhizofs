#!/bin/sh
# Wraps dpkg-buildpackage so a local/dev build (an untagged commit or a
# dirty working tree) gets a version debian will never confuse with, or
# order below, an official release upload. See scripts/get-version.sh.
set -e

cd "$(dirname "$0")/.."

CHANGELOG=debian/changelog
SUFFIX=$(scripts/get-version.sh)

if [ -n "${SUFFIX}" ]; then
    # a ".orig" suffix would get swept up by dh_clean (run by
    # dpkg-buildpackage's pre-build clean) as a stray patch artifact, so
    # keep the backup outside debian/ entirely.
    BACKUP=$(mktemp)
    cp "${CHANGELOG}" "${BACKUP}"
    trap 'mv "${BACKUP}" "${CHANGELOG}"' EXIT

    {
        head -n1 "${CHANGELOG}" | sed -E "s/^(rhizofs \([0-9][^)-]*)(-[0-9]+\))/\1+${SUFFIX}\2/"
        tail -n +2 "${CHANGELOG}"
    } > "${CHANGELOG}.new"
    mv "${CHANGELOG}.new" "${CHANGELOG}"
fi

dpkg-buildpackage -uc -us
