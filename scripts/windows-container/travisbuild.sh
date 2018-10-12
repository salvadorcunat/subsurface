#!/bin/bash

# this is run to actually trigger the creation of the Windows installer
# inside the container

#set -x
set -e
RED="\033[0;31m"
DEFAULT="\033[0m"
echo -e "$RED
***********************************************************************
************************ Begin travisbuild.sh *************************
***********************************************************************
$DEFAULT
"
docker exec -t builder bash subsurface/scripts/windows-container/in-container-build.sh | tee build.log

# fail the build if we didn't create the target binary
grep "Built target installer" build.log

