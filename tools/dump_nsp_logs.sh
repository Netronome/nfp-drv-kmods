#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
# Copyright (C) 2015-2017 Netronome Systems, Inc.

# Script for extracting NSP logs.  If any nfp netdevs exist on the system,
# the script will fetch logs from them.  Otherwise it will try to (re)load
# the driver with nfp_dev_cpp=1 and get them directly.

[ -z "$OUTPUT" ] && OUTPUT=/tmp/nfp_nsp_logs_$(date '+%F_%H_%M')
LOADED=

mkdir $OUTPUT

function cleanup {
    rm -rf $OUTPUT
    if [ -n "$LOADED" ]; then
	modprobe -r nfp
    fi
}
trap cleanup EXIT

# Gather some very basic info
date > $OUTPUT/date 2>&1
uname -a > $OUTPUT/uname 2>&1
lspci -d 19ee: -vv > $OUTPUT/lspci 2>&1

# Check if we have any NFP netdevs
if ls /sys/bus/pci/drivers/nfp/*/net/ > /dev/null 2>&1; then
    for ifc in $(ls /sys/bus/pci/drivers/nfp/*/net/); do
	ethtool -W $ifc 0
	ethtool -w $ifc data $OUTPUT/$ifc
    done
else
    # No netdevs, try the direct access method
    if ! modinfo nfp | grep nfp_dev_cpp > /dev/null 2>&1; then
	echo "ERROR: no nfp netdevs present, and driver doesn't support CPP access"
	exit 1
    fi
    if ! nfp-nsp --help > /dev/null 2>&1; then
	echo "ERROR: no nfp netdevs present, and nfp-nsp tool not found"
	exit 1
    fi

    if [ -d /sys/module/nfp ]; then
	if ! [ -f /sys/module/nfp/parameters/nfp_dev_cpp -a \
	       "$(cat /sys/module/nfp/parameters/nfp_dev_cpp)" == "1" ]; then
	    echo "WARNING: Reloading the nfp driver with CPP access enabled"
	    modprobe -r nfp
	fi
    fi

    if ! [ -d /sys/module/nfp ]; then
	modprobe nfp nfp_dev_cpp=1 nfp_pf_netdev=0
	LOADED=y
    fi

    for dev in $(lspci -d 19ee: | awk '{print$1}'); do
	nfp-arm -Z "$dev" -D > "$OUTPUT/$dev"
    done
fi

(
    cd $(dirname $OUTPUT)
    base=$(basename $OUTPUT)
    tar -zcvf "$base.tgz" "$base" > /dev/null
)
echo -e "INFO: NSP logs captured in \e[;1m$OUTPUT.tgz\e[;0m"
