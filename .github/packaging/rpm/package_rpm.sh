#!/bin/bash

usage() {
    cat <<EOH

Usage: .github/packaging/rpm/package_rpm.sh $NFP_DRV_REPO $PKG_NAME $PACKAGE_VERSION $PACKAGE_REVISION

Copyright (C) 2022 Corigine, Inc

Builds DKMS RPM packages. This script should not be run manually, instead it should be called
from the package-dkms.sh script under the scripts directory.

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
OUTDIR="${SRCDIR}/rpm"


PKG_NAME=$2
PKG_VERSION=$3
PKG_REVISION=$4
FULL_PKG_VERSION=${PKG_VERSION}-${PKG_REVISION}

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
    rm -rf ${OUTDIR}/${PKG_NAME}
    cd ${tmpdir}
    mkdir -p ${OUTDIR}/${PKG_NAME}
}

output_manifest () {
    tmpdir=$(pwd)
    echo "MANIFEST:${PKG_NAME}-${FULL_PKG_VERSION}" > ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    cd ${SRCDIR}
    tag=$(git rev-parse HEAD)
    branch=$(git rev-parse --abbrev-ref HEAD)
    url=$(git config --get remote.origin.url)
    cd ${tmpdir}
    echo "DATE:$(date -u +%Y.%m.%d.%H%M)" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    echo "BRANCH:${branch}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    echo "TAG:${tag}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    echo "URL:${url}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    echo "" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    echo "HOST INFO:" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    host_name=$(hostname)
    kernel_version=$(uname -r)
    os_info=$(cat /etc/os-release  | head -n 2 | sed 's/NAME/OS_NAME/g' | sed 's/VERSION/OS_VERSION/g')
    echo "HOSTNAME:${host_name}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    echo "KERNEL_VERSION:${kernel_version}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
    echo "${os_info}" >> ${OUTDIR}/${PKG_NAME}/${PKG_NAME}-dkms-${FULL_PKG_VERSION}".manifest"
}

build_nfp_drv_kmod_dkms () {
    mkdir -p ${BUILDDIR}/${PKG_NAME}
    cp -r ${SRCDIR}/src ${BUILDDIR}/${PKG_NAME}/src
    echo ${FULL_PKG_VERSION} > ${BUILDDIR}/${PKG_NAME}/src/.revision
    cp_from_template ${SRCDIR}/.github/packaging/dkms.conf ${BUILDDIR}/${PKG_NAME}/dkms.conf
    cp -r ${SRCDIR}/Makefile ${BUILDDIR}/${PKG_NAME}/Makefile
    ln -sf ${BUILDDIR}/${PKG_NAME} /usr/src/${PKG_NAME}-${FULL_PKG_VERSION}

    mkdir -p ${BUILDDIR}/rpmbuild/{BUILD,RPMS,SRPMS,SPECS,SOURCES}
    cp_from_template ${BASEDIR}/nfp-drv-kmods-dkms.spec.in ${BUILDDIR}/rpmbuild/SPECS/${PKG_NAME}-dkms.spec
    mkdir -p ${BUILDDIR}/rpmbuild/SOURCES/${PKG_NAME}-${FULL_PKG_VERSION}
    cp ${SRCDIR}/.github/packaging/common.postinst ${BUILDDIR}/rpmbuild/SOURCES

    dkms add ${PKG_NAME}/${FULL_PKG_VERSION} --rpm_safe_upgrade

    cp ${BUILDDIR}/rpmbuild/SOURCES/common.postinst \
    ${BUILDDIR}/rpmbuild/SOURCES/${PKG_NAME}-${FULL_PKG_VERSION}/common.postinst

    cp -Lpr /var/lib/dkms/${PKG_NAME}/${FULL_PKG_VERSION}/source/* \
    ${BUILDDIR}/rpmbuild/SOURCES/${PKG_NAME}-${FULL_PKG_VERSION}

    source_working_dir=$(pwd)
    cd ${BUILDDIR}/rpmbuild
    rpmbuild --define "_topdir ${BUILDDIR}/rpmbuild" \
        --define "version ${PKG_VERSION}" \
        --define "release ${PKG_REVISION}" \
        --define "module_name ${PKG_NAME}" \
        --define "mktarball_line none" \
        --define "__find_provides  /usr/lib/dkms/find-provides" \
        --define "_use_internal_dependency_generator 0" \
        -ba ${BUILDDIR}/rpmbuild/SPECS/${PKG_NAME}-dkms.spec
    cd ${source_working_dir}
    cp ${BUILDDIR}/rpmbuild/RPMS/noarch/*.rpm ${OUTDIR}/${PKG_NAME}/
}

cleanup () {
    echo "CLEANUP"
    echo "${PKG_NAME}/${FULL_PKG_VERSION}"
    dkms remove ${PKG_NAME}/${FULL_PKG_VERSION} --all

    rm -rf /usr/src/${PKG_NAME:?"PKG_NAME not defined"}-${FULL_PKG_VERSION:?"FULL_PKG_VERSION not defined"}
    rm -rf ${BUILDDIR:?"BUILDDIR not defined"}
}

prepare
build_nfp_drv_kmod_dkms
output_manifest
cleanup
