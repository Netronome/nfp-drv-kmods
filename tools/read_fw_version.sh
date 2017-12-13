#!/bin/bash
#
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

# Wrapper script for extracting MIP name/version from firmware image file.

WILDCARD_PATTERN='*.nffw'
FILTERS="/lib/firmware/netronome /opt/netronome/firmware"

usage() {
    echo "Usage:  $(basename $0) [ filter ]"
    echo " Arguments:"
    echo "  filter    : file or directory to evaluate instead of the default"
    echo "              locations: $FILTERS"
    exit 1
}

[[ $1 = "--help" || $1 = "-h" || "$#" -gt 1 ]] && usage
[ -n "$1" ] && FILTERS=$1

output=""
for search_item in $FILTERS; do
    if [ -d "$search_item" ]; then
        path=$search_item
        pattern=$WILDCARD_PATTERN
    elif [ -e "$search_item" ]; then
        path=$(dirname $search_item)
        pattern=$(basename $search_item)
    else
        continue
    fi

    for fw in $(find "$path" -name "$pattern" 2>/dev/null); do
        version=$(readelf -p .note.build_info $fw 2>/dev/null | \
                  grep -Eo 'Name:.*$' | cut -d' ' -f2)
        [ -z "$version" ] && version="<UNKNOWN>"
        tag=""
        [ -L "$fw" ] && tag="symlink"
        output+="$fw $version $tag\n"
    done
done

if [ -z "$output" ]; then
    echo "No firmware images found!" >&2
    exit 1
fi
echo -e "$output" | column -t
