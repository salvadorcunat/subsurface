#!/bin/bash

set -x
set -e

# when running this locally, set TRAVIS_BUILD_DIR to the Subsurface
# directory inside your Windows build tree
TRAVIS_BUILD_DIR=${TRAVIS_BUILD_DIR:-$PWD}

git fetch --unshallow || true # if running locally, unshallow could fail
git pull --tags origin master
git submodule init
git describe

# make sure we have libdivecomputer
echo "Get libdivecomputer"
cd ${TRAVIS_BUILD_DIR}
git submodule update --recursive
cd libdivecomputer
autoreconf --install
autoreconf --install

# the intended layout as seen inside the container is
# /win/subsurface # sources that we are testing
#     /win32      # binaries that are build
#     /grantlee
#     /libzip
#     /hidapi
#     /googlemaps
#
# the first two are mounted as volumes (this way we get access to the
# build results outside of the container
cd ${TRAVIS_BUILD_DIR}/..
mkdir -p win32
rm -rf win32/*

# start the container and keep it running
docker run -v $PWD/win32:/win/win32 -v $PWD/subsurface:/win/subsurface --name=builder -w /win -d dirkhh/mxe-build-container:0.6 /bin/sleep 60m

# for some reason this package was installed but still isn't there?
docker exec -t builder apt-get install -y ca-certificates

# now set up our other dependencies
# these are either not available in MXE, or a version that's too old
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . libzip
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . hidapi
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . googlemaps
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . grantlee

# enable smtk2ssrf
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . mdbtools

# get prebuilt static mxe libraries for glib.
# do not overwrite upstream prebuilt mxe binaries if there is any coincidence.
docker exec -t builder wget -q https://www.dropbox.com/s/fesfbzqzkgee5ut/mxe-static-minimal-97c0fbfd_1.tar.xz
docker exec -t builder tar -C /win/mxe -xJf mxe-static-minimal-97c0fbfd_1.tar.xz --skip-old-files
