#!/bin/sh
# Print a snapshot suffix such as "git30.b8a4ec6" or "git30.b8a4ec6.dirty"
# identifying how far the current checkout is from a clean, tagged release
# commit. Prints nothing when HEAD is an unmodified checkout of a release
# tag, or when there is no git checkout at all (e.g. building from a
# release tarball) -- callers should use the plain release version in
# that case.
#
# debian/rules and rhizofs.spec use this to give local/dev package builds
# a version that can never be mistaken for, or collide with, an official
# release upload.
set -e

cd "$(dirname "$0")/.."

if [ ! -e .git ]; then
    exit 0
fi

if ! GIT_ERR=$(git rev-parse --is-inside-work-tree 2>&1 >/dev/null); then
    echo "get-version.sh: warning: .git exists but git failed (${GIT_ERR}); building without a snapshot suffix" >&2
    exit 0
fi

DESCRIBE=$(git describe --tags --long --match 'v[0-9]*' 2>/dev/null) || exit 0
COMMIT_COUNT=$(printf '%s' "${DESCRIBE}" | sed -E 's/^.*-([0-9]+)-g[0-9a-f]+$/\1/')
SHORT_HASH=$(printf '%s' "${DESCRIBE}" | sed -E 's/^.*-[0-9]+-g([0-9a-f]+)$/\1/')

DIRTY=""
git diff --quiet HEAD -- 2>/dev/null || DIRTY=".dirty"

if [ "${COMMIT_COUNT}" = "0" ] && [ -z "${DIRTY}" ]; then
    exit 0
fi

echo "git${COMMIT_COUNT}.${SHORT_HASH}${DIRTY}"
