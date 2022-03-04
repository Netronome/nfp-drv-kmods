#!/bin/bash

usage() {
    cat <<EOH

Usage: .github/scripts/package.sh [ OPTIONS ]

Copyright (C) 2022 Corigine, Inc

Create DKMS packages for nfp-drv-kmods. This script should be run
from the root directory of the repository. This packages must be
created on the target OS.

The following options can be provided:
 -t package_type: Select the output package type. Select one of:
             r: .rpm
             d: .deb

EOH
    exit 1
}

# ___MAIN___
DESCRIBE=""
PACKAGE_TYPE=""

unset GENERATE_DEB
unset GENERATE_RPM


while getopts "t:h" opt; do
    case $opt in
        "?"|h)
            usage
            ;;
        t)  case $OPTARG in
                r)  PACKAGE_TYPE=".rpm"
                    GENERATE_RPM=1
                    ;;
                d)  PACKAGE_TYPE=".deb"
                    GENERATE_DEB=1
                    ;;
                *) echo "Invalid package type selected"
                    ;;
            esac
            ;;
    esac
done


if [ -z $PACKAGE_TYPE ]; then
    echo "Please provide desired package type"
    usage
fi

# Navigate to the root directory of the repository
PACKAGE_DKMS_SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}" )" && pwd)
cd $PACKAGE_DKMS_SCRIPT_DIR/../..
NFP_DRV_REPO=$(pwd)

PACKAGE_NAME=$(.github/scripts/describe-head.sh --pkg_name)
PACKAGE_VERSION=$(.github/scripts/describe-head.sh --pkg_ver)
PACKAGE_REVISION=$(.github/scripts/describe-head.sh --pkg_rev)

echo "Packaging details:"
echo "    Package name:     $PACKAGE_NAME"
echo "    Package type:     $PACKAGE_TYPE"
echo "    Package version:  $PACKAGE_VERSION"
echo "    Package revision: $PACKAGE_REVISION"

if [ -n "$GENERATE_DEB" ]; then
    # Check if Revision is greater than Zero
    if [[ $PACKAGE_REVISION != "0" ]]; then
        PACKAGE_VERSION=$PACKAGE_VERSION-$PACKAGE_REVISION
    fi
    .github/packaging/deb/package_deb.sh $NFP_DRV_REPO $PACKAGE_NAME $PACKAGE_VERSION
fi

if [ -n "$GENERATE_RPM" ]; then
    .github/packaging/rpm/package_rpm.sh $NFP_DRV_REPO $PACKAGE_NAME $PACKAGE_VERSION $PACKAGE_REVISION
fi
