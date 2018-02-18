#!/bin/bash

set -x
set -e

# This comes from subsurface travisbuild.sh script.
# Keep it for now.

export PATH=$QT_ROOT/bin:$PATH # Make sure correct qmake is found on the $PATH for linuxdeployqt
export CMAKE_PREFIX_PATH=$QT_ROOT/lib/cmake

# the global build script expects to be called from the directory ABOVE subsurface
# debug
pwd
cd ${TRAVIS_BUILD_DIR}/..

bash -e -x ./subsurface/scripts/smtk2ssrf-build.sh

export QT_PLUGIN_PATH=$QT_ROOT/plugins
export QT_QPA_PLATFORM_PLUGIN_PATH=$QT_ROOT/plugins
export QT_DEBUG_PLUGINS=1

# for debugging: find $QT_ROOT/plugins

env CTEST_OUTPUT_ON_FAILURE=1 make -C subsurface/build check

mkdir -pv ./smtk2ssrf_appdir/usr/share/metainfo
mkdir -pv ./smtk2ssrf_appdir/icons/hicolor/256x256/apps
mkdir -pv ./smtk2ssrf_appdir/usr/plugins
mkdir -pv ./smtk2ssrf_appdir/usr/bin
mkdir -pv ./smtk2ssrf_appdir/usr/lib/qt5/plugins
cp -vf subsurface/icons/subsurface-icon.svg smtk2ssrf_appdir/
cp -vf subsurface/smtk-import/smtk2ssrf.desktop smtk2ssrf_appdir/
cp -vf install-root/bin/smtk2ssrf smtk2ssrf_appdir/usr/bin/
cp -rvf appdir/usr/plugins/* smtk2ssrf_appdir/usr/plugins/
unset QTDIR
unset QT_PLUGIN_PATH
unset LD_LIBRARY_PATH

./linuxdeployqt*.AppImage ./smtk2ssrf_appdir/smtk2ssrf.desktop -bundle-non-qt-libs -verbose=2
./linuxdeployqt*.AppImage ./smtk2ssrf_appdir/smtk2ssrf.desktop -appimage -verbose=2
find ./smtk2ssrf_appdir -executable -type f -exec ldd {} \; | grep " => /usr" | cut -d " " -f 2-3 | sort | uniq

