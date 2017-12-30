#!/bin/bash -eu

# run this in the top level folder you want to create Android binaries in
#
# it seems that with Qt5.7 (and later) and current cmake there is an odd bug where
# cmake fails reporting :No known features for CXX compiler "GNU". In that
# case simly comment out the "set(property(TARGET Qt5::Core PROPERTY...)"
# at line 101 of
# Qt/5.7/android_armv7/lib/cmake/Qt5Core/Qt5CoreConfigExtras.cmake 
# or at line 95 of
# Qt/5.8/android_armv7/lib/cmake/Qt5Core/Qt5CoreConfigExtras.cmake
# or at line 105 of
# Qt/5.9/android_armv7/lib/cmake/Qt5Core/Qt5CoreConfigExtras.cmake
# (this script tries to do this automatically)

set -x # make debugging Travis easier

exec 1> >(tee ./build.log) 2>&1

USE_X=$(case $- in *x*) echo "-x" ;; esac)

# these are the current versions for Qt, Android SDK & NDK:

QT_VERSION=5.9
LATEST_QT=5.9.1
NDK_VERSION=r14b
SDK_VERSION=3859397  # if you change this version, you'll likely have to change the android-sdk-license key fallback below

ANDROID_NDK=android-ndk-${NDK_VERSION}
ANDROID_SDK=android-sdk-linux

PLATFORM=$(uname)

pushd $(dirname "$0")/../../
export SUBSURFACE_SOURCE=$PWD
popd

if [ "$PLATFORM" = Linux ] ; then
	QT_BINARIES=qt-opensource-linux-x64-${LATEST_QT}.run
	NDK_BINARIES=${ANDROID_NDK}-linux-x86_64.zip
	SDK_TOOLS=sdk-tools-linux-${SDK_VERSION}.zip
else
	echo "only on Linux so far"
	exit 1
fi

# make sure we have the required commands installed
MISSING=
for i in git cmake autoconf libtool java ant wget unzip; do
	command -v $i >/dev/null ||
		if [ $i = libtool ] ; then
			MISSING="${MISSING}libtool-bin "
		elif [ $i = java ] ; then
			MISSING="${MISSING}openjdk-8-jdk "
		else
			MISSING="${MISSING}${i} "
		fi
done
if [ "$MISSING" ] ; then
	echo "The following packages are missing: $MISSING"
	echo "Please install via your package manager."
	exit 1
fi

# first we need to get the Android SDK and NDK
if [ ! -d $ANDROID_NDK ] ; then
	if [ ! -f $NDK_BINARIES ] ; then
		wget -q https://dl.google.com/android/repository/$NDK_BINARIES
	fi
	unzip -q $NDK_BINARIES
fi

if [ ! -d $ANDROID_SDK ] ; then
	if [ ! -f $SDK_TOOLS ] ; then
		wget -q https://dl.google.com/android/repository/$SDK_TOOLS
	fi
	mkdir $ANDROID_SDK
	pushd $ANDROID_SDK
	unzip -q ../$SDK_TOOLS
	yes | tools/bin/sdkmanager --licenses > /dev/null 2>&1 || echo "d56f5187479451eabf01fb78af6dfcb131a6481e" > licenses/android-sdk-license
	cat licenses/android-sdk-license
	echo ""
	# FIXME: Read these from build.sh varables, or install them there.
	tools/bin/sdkmanager tools platform-tools 'platforms;android-27' 'build-tools;25.0.3'
	popd
fi

# download the Qt installer including Android bits and unpack / install
QT_DOWNLOAD_URL=https://download.qt.io/archive/qt/${QT_VERSION}/${LATEST_QT}/${QT_BINARIES}
if [ ! -d Qt ] ; then
	if [ ! -f ${QT_BINARIES} ] ; then
		wget -q ${QT_DOWNLOAD_URL}
	fi
	chmod +x ./${QT_BINARIES}
	./${QT_BINARIES} --platform minimal --script "$SUBSURFACE_SOURCE"/qt-installer-noninteractive.qs --no-force-installations
fi

# patch the cmake / Qt5.7.1 incompatibility mentioned above
sed -i 's/set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_FEATURES cxx_decltype)/# set_property(TARGET Qt5::Core PROPERTY INTERFACE_COMPILE_FEATURES cxx_decltype)/' Qt/${LATEST_QT}/android_armv7/lib/cmake/Qt5Core/Qt5CoreConfigExtras.cmake

if [ ! -d subsurface/libdivecomputer/src ] ; then
	pushd subsurface
	git submodule init
	git submodule update --recursive
	popd
fi

if [ ! -f subsurface/libdivecomputer/configure ] ; then
	pushd subsurface/libdivecomputer
	autoreconf --install
	autoreconf --install
	popd
fi

# and now we need a monotonic build number...
if [ ! -f ./buildnr.dat ] ; then
	BUILDNR=0
else
	BUILDNR=$(cat ./buildnr.dat)
fi
BUILDNR=$((BUILDNR+1))
echo "${BUILDNR}" > ./buildnr.dat

echo "Building Subsurface-mobile for Android, build nr ${BUILDNR}"

rm -f ./subsurface-mobile-build-arm/build/outputs/apk/*.apk
rm -df ./subsurface-mobile-build-arm/AndroidManifest.xml

if [ "$USE_X" ] ; then
	bash "$USE_X" "$SUBSURFACE_SOURCE"/packaging/android/build.sh -buildnr "$BUILDNR" arm "$@"
else
	bash "$SUBSURFACE_SOURCE"/packaging/android/build.sh -buildnr "$BUILDNR" arm "$@"
fi

ls -l ./subsurface-mobile-build-arm/build/outputs/apk/*.apk

