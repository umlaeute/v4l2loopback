#!/bin/sh
####################################
# prepare package for release

# DONE: get current version from module source
# DONE: ChangeLog generator using git-dch
# DONE: update dkms.conf
# TODO: automatically update AUTHORS
# TODO: automatically prepare NEWS (from ChangeLog)
# TODO: automatically launch editors for ChangeLog/NEWS/AUTHORS
# TODO: automatically tag (if all went well)

CHANGELOG=ChangeLog
AUTHORS=AUTHORS
NEWS=NEWS
: "${mainbranch:=main}"

error() {
  echo "$@" 1>&2
}
fatal() {
  error "$@"
  exit 1
}
usage() {
 fatal "usage: $0 [<LASTVERSION>] <CURVERSION>" 1>&2
}

getoldversion() {
  dpkg-parsechangelog --count 1 -l${CHANGELOG} | grep -E "^Version:" | head -1 | cut -f2 -d' '
}
getmoduleversion() {
  grep "^#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION" v4l2loopback.c \
  | sed -e 's|^#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION||' \
        -e 's|^[^0-9]*||' -e 's|[^0-9]*$||' \
        -e 's|[^0-9][^0-9]*|.|g'
}
getmoduleversion_() {
  grep "^[[:space:]]*#[[:space:]]*define[[:space:]]*V4L2LOOPBACK_VERSION_$1[[:space:]]" v4l2loopback.h | awk '{print $NF}'
}
getmoduleversion() {
  echo "$(getmoduleversion_ MAJOR).$(getmoduleversion_ MINOR).$(getmoduleversion_ BUGFIX)"
}
getgitbranch() {
  git rev-parse --abbrev-ref HEAD
}

if [ "$(getgitbranch)" != "${mainbranch}" ]; then
 fatal "current branch '$(getgitbranch)' is not '${mainbranch}'"
fi

moduleversion=$(getmoduleversion)

if [ -z "$2" ]; then
## guess current version
 NEWVERSION=$1
 OLDVERSION=$(getoldversion)
else
 OLDVERSION=$1
 NEWVERSION=$2
fi

if [ -z "${NEWVERSION}" ]; then
  NEWVERSION="${moduleversion}"
fi

echo "module version: ${moduleversion}"

echo "updating from: ${OLDVERSION}"
if git tag -l "v${OLDVERSION}" | grep . >/dev/null
then
 :
else
 fatal "it seems like there is no tag 'v${OLDVERSION}'"
fi

if [ -z "${OLDVERSION}" ]; then
 usage
fi


if [ -z "${NEWVERSION}" ]; then
 usage
fi

echo "updating to: ${NEWVERSION}"
if dpkg --compare-versions "${OLDVERSION}" ge "${NEWVERSION}"
then
 fatal "version mismatch: ${NEWVERSION} is not newer than ${OLDVERSION}"
fi


if [ "${NEWVERSION}" != "${moduleversion}" ]; then
  echo "${NEWVERSION}" | sed -e 's|\.| |g' | while read major minor bugfix; do
    major=$((major+0))
    minor=$((minor+0))
    bugfix=$((bugfix+0))
    sed -e "s|^\([[:space:]]*#[[:space:]]*define[[:space:]]*V4L2LOOPBACK_VERSION_MAJOR[[:space:]]\).*|\1${major}|"   -i v4l2loopback.h
    sed -e "s|^\([[:space:]]*#[[:space:]]*define[[:space:]]*V4L2LOOPBACK_VERSION_MINOR[[:space:]]\).*|\1${minor}|"   -i v4l2loopback.h
    sed -e "s|^\([[:space:]]*#[[:space:]]*define[[:space:]]*V4L2LOOPBACK_VERSION_BUGFIX[[:space:]]\).*|\1${bugfix}|" -i v4l2loopback.h
    break
  done
fi

OK=false
mkdir debian
cp "${CHANGELOG}" debian/changelog
gbp dch -R --since "v${OLDVERSION}" -N "${NEWVERSION}" --debian-branch="${mainbranch}" && cat debian/changelog > "${CHANGELOG}" && OK=true
rm -rf debian

if [ "${OK}" = "true" ]; then
  sed -e "s|^PACKAGE_VERSION=\".*\"$|PACKAGE_VERSION=\"${NEWVERSION}\"|" -i dkms.conf
fi




if [ "${OK}" = "true" ]; then
 echo "all went well"
 echo ""
 echo "- please check your ${CHANGELOG}"
 echo "- please check&edit your ${NEWS}"
 echo "- please check&edit your ${AUTHORS}"
 echo "- and don't forget to git-tag the new version as v${NEWVERSION}"
 echo " git tag v${NEWVERSION} -s -m \"Released ${NEWVERSION}\""
fi
