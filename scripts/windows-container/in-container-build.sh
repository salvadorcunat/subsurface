#!/bin/bash
# this gets executed inside the container when building a Windows
# installer on Travis
#
# working directory is assumed to be the directory including all the
# source directories (subsurface, googlemaps, grantlee, etc)
# in order to be compatible with the assumed layout in the MXE script, we
# need to create the secondary build directory

#set -x
set -e
RED="\033[0;31m"
DEFAULT="\033[0m"
echo -e "$RED
***********************************************************************
************************ Begin in-container-build.sh ******************
***********************************************************************
$DEFAULT
"

mkdir -p win32
cd win32
bash -e ../subsurface/packaging/windows/mxe-based-build.sh installer

echo -e "$RED
***********************************************************************
************************ Begin smtk2ssrf-mxe-build.sh *****************
***********************************************************************
$DEFAULT
"
# re-enable this when smtk2ssrf is figured out
bash -e ../subsurface/packaging/windows/smtk2ssrf-mxe-build.sh -a -i
