#!/bin/bash
set -e

PACKAGE=rhizofs

# Must come before the rpmspec queries below: the spec computes %{version}
# from scripts/get-version.sh, which silently yields no snapshot suffix while
# git still refuses to touch this repo. That would name the tarball after a
# snapshot-less version while rpmbuild's own %{version} expansion, run after
# this point, expects one -- and %prep then fails to find the tarball.
# https://github.com/actions/checkout/issues/760
git config --global --add safe.directory $(pwd)

SPEC=${PACKAGE}.spec
VERSION=$(rpmspec -q --srpm --queryformat "[%{VERSION}\n]" ${SPEC})
FULLNAME=${PACKAGE}-${VERSION}
BUILD_REQUIRES=$(rpmspec -q --buildrequires ${SPEC})
TARBALL=$(rpmspec -q --srpm --queryformat "[%{SOURCE}\n]" ${SPEC})
ARCH=$(uname -m)
RPM_BUILD_DIR="$(pwd)/rpmbuild"
DIST=.ph5

tar zcf ${TARBALL} --transform "s,^,${FULLNAME}/," $(git ls-files)

tdnf install -y ${BUILD_REQUIRES}

mkdir -p ${RPM_BUILD_DIR}
mkdir -p ${RPM_BUILD_DIR}/{SOURCES,BUILD,RPMS,SRPMS}
mv ${TARBALL} ${RPM_BUILD_DIR}/SOURCES/

rpmbuild --nodeps -D "dist ${DIST}" -D "_topdir ${RPM_BUILD_DIR}" -ba ${SPEC}
