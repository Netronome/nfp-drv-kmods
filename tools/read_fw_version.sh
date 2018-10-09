#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
# Copyright (C) 2017 Netronome Systems, Inc.

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
                  grep -Eo 'Name:.*$' | awk -F ':|J' '{print $2 $4 $6}' | \
                  sed 's:\^:/:g' | tr -d ' ' | sed 's:/$::')
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
