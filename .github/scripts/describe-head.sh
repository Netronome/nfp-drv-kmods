#!/bin/bash

usage() {
  cat <<EOH

Usage: ./describe-head.sh [--pkg_name]
                          [--pkg_ver]
                          [--pkg_rev]
                          [--help]
                          [--default_branch]

Copyright (C) 2022 Corigine, Inc

    Generates various package and version descriptions based on current
    branch. This script should be run from the root directory of the
    nfp-drv-kmods repo.
    NOTE: Only one argument can be called at a time. If no arguments are
          specified, the Package Version will be returned.

optional arguments:
    --pkg_name        full package name
    --pkg_ver         package version
    --pkg_rev         package revision
    --help            output this prompt
    --default_branch  returns the default branch

environment variables:
    HEAD_REF        Ref name that takes priority over the automatic
                    determination of the branch. This can be any ref
                    for example a TAG name and is not just applicable to
                    branches.
    BUILD_DATE      Variable that can be used to specify the build date,
                    if it is not set then the build date is determined
                    automatically.

EOH
}

if [ "$#" -ge 2 ]; then
    echo -e "::error::Too many arguments\n" 1>&2
    usage
    exit 1
fi

HASH="$(git rev-parse --short HEAD)"
DATE="${BUILD_DATE:-$(date -u +%y.%m.%d)}"
BRANCH="${HEAD_REF:-$(git rev-parse --abbrev-ref HEAD)}"
CHANGES=""

# Find total number of commits
COMMIT_CNT="$(git rev-list --count --no-merges HEAD)"

# The default specified here will dictate what
# branch constitutes an interim build
DEFAULT="${DEFAULT_BRANCH:-public-main}"
if [ "$1" = "--default_branch" ] ; then
    echo $DEFAULT
    exit 0
fi

# Get customer name from DEFAULT branch
case ${DEFAULT} in
    public-main)
      CUSTOMER=""
      CUSTOMER_STR=""
      ;;
    *"-main")
      CUSTOMER="${DEFAULT%-main}"
      CUSTOMER_STR=-${CUSTOMER}
      ;;
    *)
      CUSTOMER=""
      CUSTOMER_STR=""
      ;;
esac

# Get the branch type
if [[ "$BRANCH" =~ ^${CUSTOMER}-release-[0-9][0-9]\.[0-9][0-9]\.[0-9]$ || \
      "$BRANCH" =~ ^release-[0-9][0-9]\.[0-9][0-9]\.[0-9]$ ]]; then
    BRANCH_TYPE=release-tag
elif [[ "$BRANCH" =~ ^${CUSTOMER}-prerelease-[0-9][0-9]\.[0-9][0-9]\.[0-9]-rc[0-9]$ || \
        "$BRANCH" =~ ^prerelease-[0-9][0-9]\.[0-9][0-9]\.[0-9]-rc[0-9] ]]; then
    BRANCH_TYPE=prerelease-tag
elif [[ "$BRANCH" =~ ^${CUSTOMER}-release-[0-9][0-9]\.[0-9][0-9]$ || \
        "$BRANCH" =~ ^release-[0-9][0-9]\.[0-9][0-9]$ ]]; then
    BRANCH_TYPE=release-branch
elif [[ "$BRANCH" == "$DEFAULT" ]]; then
    BRANCH_TYPE=interim
else
    BRANCH_TYPE=wip
fi

# Check for local uncommitted changes
if [ -n "$(git diff --name-only)" ] ; then
    CHANGES="+"
elif [ -n "$(git diff --staged --name-only)" ]; then
    CHANGES="+"
fi

# Branch-specific variables
if [[ "$BRANCH_TYPE" == "release-branch" ]]; then
    # If designated as a wip release-branch
    VERSION="$(echo $BRANCH | grep -oE '[0-9]{2}.[0-9]{2}').${HASH}"
    VER_MAJ="${VERSION%.*}"
    RELEASE="${HASH}"
    if [ -z "$VERSION" ]; then
        echo "::error::" \
             "A release branch must be formatted as 'release-YY.MM'" 1>&2
        exit 1
    fi
elif [[ "$BRANCH_TYPE" == "release-tag" ]]; then
    # If designated as a release-tag
    VERSION="$(echo ${BRANCH} | grep -oE '[0-9]{2}.[0-9]{2}.[0-9]+')"
    VER_MAJ="${VERSION%.*}"
    RELEASE="${VERSION##*.}"
    if [ -z "${VERSION}" ]; then
        echo "::error::" \
             "A release tag must be formatted as 'release-YY.MM.rev'" \
             "where 'rev' is the revision of that release" 1>&2
        exit 1
    fi
elif [[ "$BRANCH_TYPE" == "prerelease-tag" ]]; then
    # If designated as a prerelease-tag
    VERSION="$(echo $BRANCH | grep -oE '[0-9]{2}.[0-9]{2}.[0-9]{1,2}-rc[0-9]{1}+')"
    VER_MAJ="${VERSION%-*}"
    RELEASE="${VERSION##*-}"
    if [ -z "$VERSION" ]; then
        echo "::error::" \
             "A prerelease tag must be formatted as 'prerelease-YY.MM.rev-rcX'" \
             "where 'rev' is the revision of that prerelease and 'X' is the version" \
             "of the prerelease cycle." 1>&2
        exit 1
    fi
elif [[ "${BRANCH}" != "${DEFAULT}" ]]; then
    # Likely a wip- branch, look for a ticket ID in the form 'OVS-000'
    TICKET_ID="$(echo ${BRANCH} | grep -Eo '[A-Za-z]+[^[:alnum:]]?[0-9]+' | \
                 sed 's/[^[:alnum:]]//g' | head -1)"
fi

# Generate PACKAGE NAME
if [ "$1" = "--pkg_name" ] ; then
    # Applies to all builds, eg
    # main: agilio-nfp-driver
    # customer: agilio-nfp-driver-customer
    echo "agilio-nfp-driver${CUSTOMER_STR}"

# Generate PACKAGE RELEASE VERSION (only applies to release branches)
elif [ "$1" = "--pkg_rev" ] ; then
    if [[ "release-branch release-tag prerelease-tag" =~ $BRANCH_TYPE ]]; then
        # Monthly/Quarterly release
        echo "${RELEASE}${CHANGES}"
    else
        # Each INTERIM/TEMP build is considered a new version, so default to 0
        echo "0${CHANGES}"
    fi

# Help prompt
elif [ "$1" = "--help" ]; then
    usage
    exit 0

# Determine MAJOR PACKAGE VERSION ("--pkg_ver")
else
    COMMITS_PADDED="$(printf '%0.5d\n' ${COMMIT_CNT})"
    if [[ "release-tag release-branch prerelease-tag" =~ $BRANCH_TYPE ]]; then
        # Monthly/Quarterly release, eg
        # 22.01
        echo "${VER_MAJ}"
    elif [[ "interim" =~ $BRANCH_TYPE ]]; then
        # INTERIM build, eg
        # 22.01~01121.master.ec587408
        echo "${DATE:0:5}~${COMMITS_PADDED}.${DEFAULT//-/}.${HASH}"
    else
        # TEMPORARY build, eg
        # 22.01~01121.NIC000.ec587408
        echo "${DATE:0:5}~${COMMITS_PADDED}.${TICKET_ID:-wip}.${HASH}"
    fi
fi
