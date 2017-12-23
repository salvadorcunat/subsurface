#!/bin/bash

set -x
set -e

# This comes from subsurface travisbuild.sh script.
# Keep it for now.

export PATH=$QT_ROOT/bin:$PATH # Make sure correct qmake is found on the $PATH for linuxdeployqt
export CMAKE_PREFIX_PATH=$QT_ROOT/lib/cmake

# the global build script expects to be called from the directory ABOVE subsurface
cd ..

bash -e -x ./subsurface/scripts/smtk2ssrf-build -b Debug

