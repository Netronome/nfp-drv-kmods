#!/bin/bash

# Copyright (C) 2017 Netronome Systems, Inc.
#
# This software is dual licensed under the GNU General License Version 2,
# June 1991 as shown in the file COPYING in the top-level directory of this
# source tree or the BSD 2-Clause License provided below.  You have the
# option to license this software under the complete terms of either license.
#
# The BSD 2-Clause License:
#
#     Redistribution and use in source and binary forms, with or
#     without modification, are permitted provided that the following
#     conditions are met:
#
#      1. Redistributions of source code must retain the above
#         copyright notice, this list of conditions and the following
#         disclaimer.
#
#      2. Redistributions in binary form must reproduce the above
#         copyright notice, this list of conditions and the following
#         disclaimer in the documentation and/or other materials
#         provided with the distribution.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Script to collect state information from the OS and FW.
#
# The default result is a .tgz in the /tmp directory, of the form
# "/tmp/nfp_snapshot_yyyyMMdd_HHmmss_Z.tgz".
#
# The base directory, the archive name, and whether or not to clean up
# the temporary directory during tar, can be overridden with command
# line switch parameters, as defined below.
#
# The script executes commands and stores the stdout to files named
# after the commands. For commands that fail, the stderr output is
# stored in the resulting files, suffixed by "errno=<errno>" to expose
# the return value of the failed command.
#
# Also copy files and directories, which may not exist (these are simply
# logged and ignored).
#
# Copied files and command output files are stored under a "part"
# subdirectory, e.g. system, kernel, ovs, fw.

SCRIPT_VERSION=1.0
SCRIPT_NAME="Netronome system state collection script v${SCRIPT_VERSION}"
# defaults:
COLLECT_ARCHIVE=nfp_system_snapshot_$(date +%Y%m%d_%H%M%S_%Z)
COLLECT_BASEDIR=/tmp
TAR_RM_FILES="--remove-files"
# well defined name of dir where archive files will be placed
OUTPUT=nfp_system_snapshot_$RANDOM
MAX_LOG_LINES=$((100 * 1000))

usage() {
    echo "${SCRIPT_NAME}"
    [[ ! -z $1 ]] && echo "$1"
    echo "Usage:  $(basename $0) -b basedir -a archive -x"
    echo " Arguments:"
    echo "  -b basedir    : existing dir to place archive [/tmp]"
    echo "  -a archive    : tar base name " \
         "[nfp_system_snapshot_{yyyyMMdd_HHmmss_Z}]"
    echo "  -x            : switch to disable default cleanup of archive dir"
    exit 1
}

parse_args() {
    [[ $1 = "--help" || $1 = "-h" ]] && usage
    while getopts ":a:b:x" opt; do
        case $opt in
        a)
            COLLECT_ARCHIVE=$(echo "$OPTARG" | sed 's/[^:.[:alnum:]_-]/_/g')
            [[ -z $OPTARG || ! $COLLECT_ARCHIVE == $OPTARG ]] \
                && usage "-a '$OPTARG' has invalid characters."
            ;;
        b)
            [[ -z $OPTARG || ! -d $OPTARG ]] \
                && usage "-b '$OPTARG' is not a valid base dir."
            COLLECT_BASEDIR=$OPTARG
            ;;
        x)
            TAR_RM_FILES=""
            ;;
        \?)
            usage "Unrecognized option: -$OPTARG"
            ;;
        esac
    done
}

setup_basedir() {
    # Make sure the base dir is an absolute one
    cd $COLLECT_BASEDIR
    COLLECT_BASEDIR=$(pwd)

    # Ensure base dir exists, then make an empty $OUTPUT to be safe
    cd $COLLECT_BASEDIR \
        && rm -rf $OUTPUT \
        && mkdir $OUTPUT

    [[ $? != 0 ]] \
        && usage "Can't set up archive directory $COLLECT_BASEDIR/$OUTPUT"

    DEST="$COLLECT_BASEDIR/$OUTPUT"
}

start_logging() {
    # Tee all subsequent stdout and error data to both stdout and a log file.
    exec &> >(tee -a $DEST/script.log)
}

print_vars_used() {
    # log the configuration/parameters used
    echo "${SCRIPT_NAME}"
    echo "====================================================================="
    echo "COLLECT_BASEDIR=$COLLECT_BASEDIR"
    echo "COLLECT_ARCHIVE=$COLLECT_ARCHIVE"
    [[ -z $TAR_RM_FILES ]] && echo "No cleanup after tar (-x)."
    echo "====================================================================="
}

# Create a context dir relative to $DEST, and set up for use.
mkpart() {
    local cleaned
    local new_part

    cleaned=$(echo "$1" | sed 's/[^:.[:alnum:]_/-]/_/g')
    new_part=$DEST/$cleaned
    mkdir -p $new_part

    PART=$new_part
}

# Run command, store $OUTPUT in file, relative to current $PART dir.
# Log the command and possible error to std out.
#
# $1: Command to run
# $2: Optional name of file to store results. If not provided, use $1.
# $3: Optional name of destination directory (starting with /), relative
#     to the current $PART context.
run_cmd() {
    local cmd=$1
    local fname=$2
    local dest_dir=$3

    if [[ -z $cmd ]]; then
        echo "run_cmd() requires at least one param"
        return
    fi
    [[ -z $fname ]] && fname=$1
    # Ensure $fname has only alphanumerics, _ : or -
    fname=$dest_dir/$(echo "$fname" | sed 's/[^:.[:alnum:]_-]/_/g')
    local out=$PART$fname
    mkdir -p $(dirname $out)
    echo "executing: $cmd >> $out"
    eval "2>&1 $cmd" >> $out
    local ret=$?
    if [[ $ret != 0 ]]; then
        echo "errno=$ret" >> $out
    fi
}

# Copy a file or directory recursively to the archive.
# Ensure target directory structure is created, before copying.
# Resolve symbolic links.
#
# $1: Absolute path to source file/dir.
# $2: Path to dest file/dir, relative to $PART base dir.
#     If not provided, use $1.
archive() {
    local abs_path_to_cp=$1
    local abs_cp_dest=$PART$abs_path_to_cp

    if [[ -z $1 ]]; then
        echo "archive() requires at least one param"
        return
    fi
    if [[ ! -e $abs_path_to_cp ]]; then
        echo "$abs_path_to_cp does not exist on this machine."
        return
    fi
    [[ ! -z $2 ]] && abs_cp_dest=$PART$2
    mkdir -p $(dirname $abs_cp_dest)
    echo "Copying $abs_path_to_cp to $abs_cp_dest"
    cp -r -L $abs_path_to_cp $abs_cp_dest
}

collect_system_info() {
    mkpart system
    run_cmd date
    run_cmd uptime
    run_cmd hostname
    run_cmd "lspci -nnvvv"
    run_cmd "dpkg -l"
    run_cmd "rpm -qa"
    run_cmd lstopo

    archive /etc/hostid
    archive /etc/machine-id
    archive /etc/os-release
    archive /etc/redhat-release
    archive /etc/fedora-release
    archive /etc/lsb-release
    archive /etc/debian_version
}

collect_kernel_info() {
    mkpart kernel
    run_cmd 'uname -a' uname
    run_cmd lsmod
    run_cmd "dmesg | tail -$MAX_LOG_LINES" dmesg
    archive /proc/cmdline
    archive /etc/modprobe.conf
    archive /etc/modprobe.d
}

print_directions() {
    local tgz_path=$COLLECT_BASEDIR/$COLLECT_ARCHIVE.tgz
    local tgz_name

    tgz_name=$(basename $tgz_path)
    echo "====================================================================="
    echo "Please review the contents of $tgz_name and e-mail the file to"
    echo "support@netronome.com along with a description of the problem"
    echo "you have encountered. If possible, also provide steps to reproduce"
    echo "the problem."
    echo ""
    [[ -z $TAR_RM_FILES ]] \
        && echo "Files still in: $COLLECT_BASEDIR/$OUTPUT" && echo ""
    echo "$tgz_path"
}

create_tar() {
    # make a tgz and remove the tmp dir
    cd $COLLECT_BASEDIR \
        && tar $TAR_RM_FILES -czf $COLLECT_ARCHIVE.tgz \
               --transform "s|^$OUTPUT|$COLLECT_ARCHIVE|" $OUTPUT

    # print final state
    if [[ $? == 0 ]]; then
        print_directions
    else
        echo "Error creating tgz"
    fi
}

main() {
    parse_args "$@"
    setup_basedir
    start_logging
    print_vars_used

    collect_system_info
    collect_kernel_info

    create_tar
}

main "$@"
