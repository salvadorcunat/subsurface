#!/bin/bash

set -x
set -e

# when running this locally, set TRAVIS_BUILD_DIR to the Subsurface
# directory inside your Windows build tree
TRAVIS_BUILD_DIR=${TRAVIS_BUILD_DIR:-$PWD}

git fetch --unshallow || true # if running locally, unshallow could fail
git pull --tags
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

# start the container and keep it running
docker run -v $PWD/win32:/win/win32 -v $PWD/subsurface:/win/subsurface --name=builder -w /win -d dirkhh/mxe-build-container:0.6 /bin/sleep 60m

# for some reason this package was installed but still isn't there?
# hmmmm. The container doesn't seem to have libtool installed
docker exec -t builder apt-get install -y ca-certificates libtool

# now set up our other dependencies
# these are either not available in MXE, or a version that's too old
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . libzip
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . hidapi
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . googlemaps
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . grantlee

# smtk2ssrf build
docker exec -t builder bash subsurface/scripts/get-dep-lib.sh single . mdbtools

# get prebuilt static mxe libraries for glib.
# do not overwrite upstream prebuilt mxe binaries if there is any coincidence.
echo -n "Downloading prebuilt static mxe ... "
docker exec -t builder wget -q https://www.dropbox.com/s/2ahfkyi6rhbihtn/mxe-static-minimal-a08b3225.tar.xz
echo -n "Untarring ... "
docker exec -t builder tar -C /win/mxe -xJf mxe-static-minimal-a08b3225.tar.xz --skip-old-files
echo "Done."
docker exec -t builder ln -vs /win/mxe /usr/src/mxe
