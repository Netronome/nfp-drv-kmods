#!/bin/bash

usage() {
    cat <<EOH

Usage: .github/packaging/tarball/package_tar.sh \$NFP_DRV_REPO \$PKG_NAME \$PACKAGE_VERSION \$PACKAGE_REVISION

Copyright (C) 2024 Corigine, Inc

Builds tarball packages. These can be used to generate non-dkms .rpm packages
for specific kernel versions. This script should not be run manually, instead
it should be called from the create-packages.sh script under the scripts
directory.

EOH
    exit 1
}

if [ "$#" -ne 4 ]; then
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
OUTDIR="${SRCDIR}/tgz"


PKG_NAME=$2
PKG_VERSION=$3
PKG_REVISION=$4
FULL_PKG_VERSION=${PKG_VERSION}-${PKG_REVISION}
FULL_OUTDIR=${OUTDIR}/${PKG_NAME}/${PKG_NAME}-${FULL_PKG_VERSION}

set -xe

cp_from_template () {
    cp $1 $2
    sed -i "s/__PKG_VERSION__/${PKG_VERSION}/g" $2
    sed -i "s/__PKG_REVISION__/${PKG_REVISION}/g" $2
    sed -i "s/__PKG_NAME__/${PKG_NAME}/g" $2
}


prepare () {
    echo "Preparing build environment"
    tmpdir=$(pwd)
    cd ${SRCDIR}
    rm -rf ${OUTDIR}
    cd ${tmpdir}
    mkdir -p ${FULL_OUTDIR}
}

output_manifest () {
    tmpdir=$(pwd)
    echo "MANIFEST:${PKG_NAME}-${FULL_PKG_VERSION}" > ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    cd ${SRCDIR}
    tag=$(git rev-parse HEAD)
    branch=$(git rev-parse --abbrev-ref HEAD)
    url=$(git config --get remote.origin.url)
    cd ${tmpdir}
    echo "DATE:$(date -u +%Y.%m.%d.%H%M)" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    echo "BRANCH:${branch}" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    echo "TAG:${tag}" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    echo "URL:${url}" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    echo "" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    echo "HOST INFO:" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    host_name=$(hostname)
    kernel_version=$(uname -r)
    os_info=$(cat /etc/os-release  | head -n 2 | sed 's/NAME/OS_NAME/g' | sed 's/VERSION/OS_VERSION/g')
    echo "HOSTNAME:${host_name}" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    echo "KERNEL_VERSION:${kernel_version}" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
    echo "${os_info}" >> ${FULL_OUTDIR}/${PKG_NAME}-${FULL_PKG_VERSION}".manifest"
}

build_nfp_drv_kmod_tar () {
    mkdir -p ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}
    # Copy over sources and Makefiles
    cp -r ${SRCDIR}/src ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}/src
    echo ${FULL_PKG_VERSION} > ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}/src/.revision
    cp_from_template ${SRCDIR}/.github/packaging/tarball/nfp.spec.in ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}/nfp.spec
    cp -r ${SRCDIR}/Makefile ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}/Makefile
    cp -r ${SRCDIR}/.github/packaging/module-symverse-save.sh \
      ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}/module-symverse-save.sh

    # Copy over tools directory
    mkdir ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}/tools
    for tl in set_irq_affinity.sh profile.sh nfp_troubleshoot_gather.py; do
        cp -Lpr ${SRCDIR}/tools/${tl} ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}/tools/
    done

    # Genarate Tar that can be used with rpm build
    cd ${BUILDDIR}
    tar -czf ${PKG_NAME}-${PKG_VERSION}.tgz ${PKG_NAME}-${PKG_VERSION}
    cp ${BUILDDIR}/${PKG_NAME}-${PKG_VERSION}.tgz ${FULL_OUTDIR}/${PKG_NAME}-${PKG_VERSION}.tgz
    cd ${FULL_OUTDIR}/../
    tar -czf ${PKG_NAME}-${FULL_PKG_VERSION}.tgz ${PKG_NAME}-${FULL_PKG_VERSION}
    rm -rf ${FULL_OUTDIR:?"FULL_OUTDIR not defined"}
}

cleanup () {
    echo "CLEANUP"
    echo "${PKG_NAME}/${FULL_PKG_VERSION}"
    rm -rf ${BUILDDIR:?"BUILDDIR not defined"}
}

prepare
# Clean atrifacts from a preveous build that may have failed and that did not cleanup properly.
set +e # Ignore errors since it is expected that some things might be missing at this point.
cleanup
set -e # Reset to not ignore errors.
output_manifest
build_nfp_drv_kmod_tar
cleanup
