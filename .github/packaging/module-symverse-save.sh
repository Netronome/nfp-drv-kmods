#!/usr/bin/bash

dkms_tree=$1
kernel_ver=$2
module_location=$3
mode=$4

SRC_DIR=${dkms_tree}/${module}/${module_version}/build/src
DST_DIR=/lib/modules/${kernel_ver}${module_location}-symvers/

if [ "${mode}" == "install" ]; then
    mkdir -p ${DST_DIR}
    cp -f ${SRC_DIR}/Module.symvers ${DST_DIR}/nfp_driver.symvers
else
    rm -f ${DST_DIR}/nfp_driver.symvers
fi
