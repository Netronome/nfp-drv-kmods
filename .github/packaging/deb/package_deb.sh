#!/bin/bash

usage() {
    cat <<EOH

Usage: .github/packaging/deb/package_deb.sh $NFP_DRV_REPO $PKG_NAME $PKG_VERSION

Copyright (C) 2022 Corigine, Inc

Builds DKMS DEB packages. This script should not be run manually, instead it should be called
from the package-dkms.sh script under the scripts directory.

EOH
    exit 1
}

if [ "$#" -ne 3 ]; then
    echo -e "::error::Incorrect arguments\n" 1>&2
    usage
    exit 1
fi

SRCDIR=$1
echo "SourceDIR: ${SRCDIR}"

# This is to get the actual base dir of the script and
# not the directory from which the script was called
BASEDIR=$(cd "$(dirname "${BASH_SOURCE[0]}" )" && pwd)
BUILDDIR="${SRCDIR}/_build"
OUTDIR="${SRCDIR}/deb"

PKG_NAME=$2
PKG_VERSION=$3

set -xe

cp_from_template () {
    cp $1 $2
    sed -i "s/__KMOD_VERSION__/${PKG_VERSION}/g" $2
}

preproc_file () {
    echo "modifying $1..."
    sed -e "s/DEBIAN_PACKAGE/${PKG_NAME}/g" \
       -e "s/DEBIAN_BUILD_ARCH/all/g" \
       -e "s/KERNEL_VERSION/$(uname -r)/g" \
        -e "s/MODULE_NAME/${PKG_NAME}/g" \
        -e "s/MODULE_VERSION/${PKG_VERSION}/g" \
        -e "s/DATE_STAMP/$(date -R)/" "$1" > "$1.dkms-pp"
    mv "$1.dkms-pp" "$1"
}

prepare () {
    echo "Preparing build environment"
    rm -rf ${OUTDIR}/${PKG_NAME}

    mkdir -p ${OUTDIR}/${PKG_NAME}
}

output_manifest () {
    tmpdir=$(pwd)
    echo "MANIFEST:${PKG_NAME}-${PKG_VERSION}" > ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    cd ${SRCDIR}
    tag=$(git rev-parse HEAD)
    branch=$(git rev-parse --abbrev-ref HEAD)
    url=$(git config --get remote.origin.url)
    cd ${tmpdir}
    echo "DATE:$(date -u +%Y.%m.%d.%H%M)" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    echo "BRANCH:${branch}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    echo "TAG:${tag}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    echo "URL:${url}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    echo "" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    echo "HOST INFO:" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    host_name=$(hostname)
    kernel_version=$(uname -r)
    os_info=$(cat /etc/os-release  | head -n 2 | sed 's/NAME/OS_NAME/g' | sed 's/VERSION/OS_VERSION/g')
    echo "HOSTNAME:${host_name}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    echo "KERNEL_VERSION:${kernel_version}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
    echo "${os_info}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${PKG_VERSION}".manifest"
}

build_nfp_drv_kmod_dkms () {
    mkdir -p ${BUILDDIR}/${PKG_NAME}
    cp -r ${SRCDIR}/src ${BUILDDIR}/${PKG_NAME}/src
    echo ${PKG_REVISION} > ${BUILDDIR}/${PKG_NAME}/.revision
    cp_from_template ${SRCDIR}/.github/packaging/dkms.conf ${BUILDDIR}/${PKG_NAME}/dkms.conf
    cp -r ${SRCDIR}/Makefile ${BUILDDIR}/${PKG_NAME}/Makefile

    ln -sf ${BUILDDIR}/${PKG_NAME} /usr/src/${PKG_NAME}-${PKG_VERSION}
    dkms add ${PKG_NAME}/${PKG_VERSION}

    # Prepare files for dpkg-buildpackage
    SOURCES_DEB_DIR=${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/${PKG_NAME}-${PKG_VERSION}
    mkdir -p ${SOURCES_DEB_DIR}
    cp -r ${BUILDDIR}/${PKG_NAME}/* ${SOURCES_DEB_DIR}/
    cp ${SRCDIR}/.github/packaging/deb/Makefile ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/Makefile
    cp ${SRCDIR}/.github/packaging/common.postinst \
    ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/common.postinst
    mkdir -p ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/debian
    cp -r ${SRCDIR}/.github/packaging/deb/debian/* ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/debian/.

    for file in ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/debian/*; do
        preproc_file "${file}"
    done

    # Set permissions required by dpkg-buildpackage
    chmod 755 ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/debian/prerm
    chmod 755 ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/debian/rules
    chmod 755 ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/debian/postrm
    chmod 755 ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}/debian/postinst

    temp_dir=$(pwd)
    cd ${BUILDDIR}/${PKG_NAME}-dkms-${PKG_VERSION}
    dpkg-buildpackage -d -b -us -uc
    cd ${temp_dir}

    cp ${BUILDDIR}/*.deb ${OUTDIR}/${PKG_NAME}/.
    cp ${BUILDDIR}/*.buildinfo ${OUTDIR}/${PKG_NAME}/.
    cp ${BUILDDIR}/*.changes ${OUTDIR}/${PKG_NAME}/.
}


cleanup () {
    echo "CLEANUP"
    echo "${PKG_NAME}/${PKG_VERSION}"
    dkms remove ${PKG_NAME}/${PKG_VERSION} --all

    rm -rf /usr/src/${PKG_NAME:?"PKG_NAME not defined"}-${PKG_VERSION:?"PKG_VERSION not defined"}
    rm -rf ${BUILDDIR:?"BUILDDIR not defined"}
}

prepare
build_nfp_drv_kmod_dkms
output_manifest
cleanup
