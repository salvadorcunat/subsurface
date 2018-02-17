#!/bin/bash

set -x
set -e

# this gets executed by Travis when building an AppImage for Linux
# it gets started from inside the subsurface directory

export PATH=$QT_ROOT/bin:$PATH # Make sure correct qmake is found on the $PATH for linuxdeployqt
export CMAKE_PREFIX_PATH=$QT_ROOT/lib/cmake

# the global build script expects to be called from the directory ABOVE subsurface
# build both desktop and mobile - first desktop without BT support and without
# webkit to make sure that still works, then with all components in order
# to create an AppImage
cd ..

#bash -e -x ./subsurface/scripts/build.sh -desktop -no-bt
rm -rf subsurface/build
bash -e -x ./subsurface/scripts/build.sh -both -create-appdir -build-with-webkit

export QT_PLUGIN_PATH=$QT_ROOT/plugins
export QT_QPA_PLATFORM_PLUGIN_PATH=$QT_ROOT/plugins
export QT_DEBUG_PLUGINS=1

# for debugging: find $QT_ROOT/plugins

env CTEST_OUTPUT_ON_FAILURE=1 make -C subsurface/build check

# set up the appdir
mkdir -p appdir/usr/plugins/

# mv googlemaps and Grantlee plugins into place
mv appdir/usr/usr/local/Qt*/plugins/* appdir/usr/plugins # the usr/usr is not a typo - that's where it ends up
mv appdir/usr/lib/grantlee/ appdir/usr/plugins/
rm -rf appdir/usr/home/ appdir/usr/include/ appdir/usr/share/man/ # No need to ship developer and man files as part of the AppImage
rm -rf appdir/usr/usr appdir/usr/lib/cmake appdir/usr/lib/pkgconfig

# get the linuxdeployqt tool and run it to collect the libraries
wget -q -c "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod a+x linuxdeployqt*.AppImage
unset QTDIR
unset QT_PLUGIN_PATH
unset LD_LIBRARY_PATH
./linuxdeployqt*.AppImage ./appdir/usr/share/applications/*.desktop -bundle-non-qt-libs -qmldir=./subsurface/map-widget/ -verbose=2

# create the AppImage
export VERSION=$(cd ${TRAVIS_BUILD_DIR}/scripts ; ./get-version linux) # linuxdeployqt uses this for naming the file
./linuxdeployqt*.AppImage ./appdir/usr/share/applications/*.desktop -appimage -qmldir=./subsurface/map-widget/ -verbose=2
find ./appdir -executable -type f -exec ldd {} \; | grep " => /usr" | cut -d " " -f 2-3 | sort | uniq
bash -e -x ./subsurface/scripts/smtk2ssrf-build.sh

# Just for testing. Remove for master patch
find ./install-root
###########################################

mkdir -pv ./smtk2ssrf_appdir/usr/share/metainfo
mkdir -pv ./smtk2ssrf_appdir/icons/hicolor/256x256/apps
mkdir -pv ./smtk2ssrf_appdir/usr/plugins
mkdir -pv ./smtk2ssrf_appdir/usr/bin
mkdir -pv ./smtk2ssrf_appdir/usr/lib/qt5/plugins
cp -vf subsurface/icons/subsurface-icon.svg smtk2ssrf_appdir/
cp -vf subsurface/smtk-import/smtk2ssrf.desktop smtk2ssrf_appdir/
cp -vf install-root/bin/smtk2ssrf smtk2ssrf_appdir/usr/bin/
cp -vf install-root/lib/libdivecomputer.so.0 smtk2ssrf_appdir/usr/lib/
cp -vf install-root/lib/libgit2* smtk2ssrf_appdir/usr/lib/
cp -vrf appdir/usr/plugins/ smtk2ssrf_appdir/usr/
#ldd smtk2ssrf_appdir/usr/bin/smtk2ssrf | grep "=>" | grep -v not| grep -v vdso | awk '{print $3}' | xargs -I '{}' cp -v '{}' smtk2ssrf_appdir/usr/lib
#ldd smtk2ssrf_appdir/usr/lib/qt5/plugins/platforms/libqxcb.so | grep "=>" |grep -v not |grep -v vdso |grep -v ld-linux |awk '{print $3}' | xargs -I '{}' cp -v '{}' smtk2ssrf_appdir/usr/lib
./linuxdeployqt*.AppImage ./smtk2ssrf_appdir/smtk2ssrf.desktop -bundle-non-qt-libs -verbose=2
./linuxdeployqt*.AppImage ./smtk2ssrf_appdir/smtk2ssrf.desktop -appimage -verbose=2
find ./smtk2ssrf_appdir -executable -type f -exec ldd {} \; | grep " => /usr" | cut -d " " -f 2-3 | sort | uniq

