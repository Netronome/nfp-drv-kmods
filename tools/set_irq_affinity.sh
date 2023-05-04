#!/bin/bash -e
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
# Copyright (C) 2023 Corigine, Inc.

usage() {
    echo "Usage: $0 NETDEV CPU_CORES"
    echo "          Optional env vars: IRQ_NAME_FMT"
    echo "CPU_CORES argument optional - defaults to all local NUMA cores."
    echo "CPU_CORES format '8-23' or '8,10,12', or a combination thereof."
    echo "CPU_CORES recommended to only include NUMA cores local to the NIC."
    echo "CPU_CORES recommended to exclude cores used for user space applications such as iperf."
    exit 1
}

# Verify that at least one argument is specified
[ $# -lt 1 ] && usage

# Set IRQ queue fd format
[ "a$IRQ_NAME_FMT" == a ] && IRQ_NAME_FMT=$1-rxtx

# Find netdev specified
DEV=$1
if ! [ -e /sys/bus/pci/devices/$DEV ]; then
    DEV=$(ethtool -i $1 | grep bus | awk '{print $2}')
fi

# If netdev not found, probably incorrectly specified
[ "a$DEV" == a ] && usage

# Find local NUMA cores
if ls /sys/devices/system/node/*/cpulist >/dev/null 2>&1; then
    # Get the NUMA node of the network device
    node=$(cat /sys/bus/pci/devices/$DEV/numa_node)

    # If the NUMA node is not available, use node 0 as default
    if [ "$node" = "-1" ]; then
        node=0
    fi

    # Get the cpulist of the NUMA node of the network device
    numa_cpulist=$(cat /sys/devices/system/node/node$node/cpulist)

    # Exclude HT/SMT cores (not always reliable due to numbering).
    physical_cpulist=$(echo $numa_cpulist | sed 's/[^0-9,-]//g' | awk -F '-' '{for(i=$1;i<=$2;i++) printf "%d,", i}' | sed 's/,$//')

    echo Local NUMA cores: $physical_cpulist
fi

# Parse cores from CPU_CORES argument
CPU_ARG=$2
if [[ -z $CPU_ARG ]]; then
    # No cores specified - use NUMA cores.
    CPUS=$physical_cpulist
    echo No cores specified for IRQ binding. Using NUMA cores.
elif [[ $CPU_ARG =~ ^([0-9]+)(,([0-9]+))*(\-([0-9]+))?(,([0-9]+)(\-[0-9]+)?)*$ ]]; then
    # CPU_CORES specified: 0-4 OR 0,1,2,3,4 OR 0,1,2-4 OR 0,1,2-4,6-8
    if [[ $CPU_ARG =~ ^([0-9]+)-([0-9]+)$ ]]; then
        start=${BASH_REMATCH[1]}
        end=${BASH_REMATCH[2]}
        step=1
        CPUS=()
        for ((i=$start; i<=$end; i+=$step)); do
            CPUS+=($i)
        done
    else
        # Comma-separated list
        IFS=',' read -ra CPUS <<< "$CPU_ARG"
    fi
else
    echo "Invalid format for CPU cores argument: $CPU_ARG"
    exit 1
fi

echo Device $DEV with IRQ affinity CPUs ${CPUS[@]}

# Disable IRQ balancing
IRQBAL=$(ps aux | grep irqbalance | wc -l)
[ $IRQBAL -ne 1 ] && echo Killing irqbalance && killall irqbalance

# Identify netdev IRQ queues
IRQS=$(ls /sys/bus/pci/devices/$DEV/msi_irqs/)
IRQS=($IRQS)

# Assign SMP affinity mask & XPS state to each IRQ
for ((i=0; i<${#IRQS[@]}; i++)); do
    irq=${IRQS[i]}

    # Check if IRQ exists
    if ! [ -e /proc/irq/$irq ]; then
        continue
    fi
    name=$(basename /proc/irq/$irq/$IRQ_NAME_FMT*)
    if ! ls /proc/irq/$irq/$IRQ_NAME_FMT* >>/dev/null 2>/dev/null; then
        continue
    fi

    # Set cpu_list variable to a comma-separated list of CPU IDs
    cpu_list=$(echo "${CPUS[*]}" | tr ' ' ',')

    # Set SMP affinity list file for the current IRQ
    echo $cpu_list >/proc/irq/$irq/smp_affinity_list
    irq_state="irq: $(cat /proc/irq/$irq/smp_affinity)"

    # Set XPS control for IRQ
    xps_state='xps: ---'
    for ((j=0; j<${#CPUS[@]}; j++)); do
        xps_file=/sys/class/net/$1/queues/tx-$((i*${#CPUS[@]}+j))/xps_cpus
        if [ -e "$xps_file" ]; then
            xps_mask=$(printf "%x" $((1 << ${CPUS[j]})))
            xps_mask=$(echo $xps_mask | sed 's/\(.\)\(.\{8\}$\)/\1,\2/')
            echo $xps_mask > "$xps_file"
            xps_state="${xps_state}, $((j+1)): $(cat $xps_file)"
        fi
    done

    echo -e "IRQ $irq to CPUs $cpu_list     ($irq_state $xps_state)"
done
