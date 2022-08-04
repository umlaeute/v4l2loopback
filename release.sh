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
: ${mainbranch:=release}

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

getoldversion () {
  dpkg-parsechangelog --count 1 -l${CHANGELOG} |  egrep "^Version:" | head -1 | cut -f2 -d' '
}
getmoduleversion() {
  grep "^#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION" v4l2loopback.c \
  | sed -e 's|^#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION||' \
        -e 's|^[^0-9]*||' -e 's|[^0-9]*$||' \
        -e 's|[^0-9][^0-9]*|.|g'
}
getgitbranch() {
  git rev-parse --abbrev-ref HEAD
}

if [ "$(getgitbranch)" != "${mainbranch}" ]; then
 fatal "current branch '$(getgitbranch)' is not '${mainbranch}'"
fi

if [ "x$2" = "x" ]; then
## guess current version
 NEWVERSION=$1
 OLDVERSION=$(getoldversion)
else
 OLDVERSION=$1
 NEWVERSION=$2
fi

if [ "x${NEWVERSION}" = "x" ]; then
  NEWVERSION=$(getmoduleversion)
fi

if git tag -l v${OLDVERSION} | grep . >/dev/null
then
 :
else
 fatal "it seems like there is no tag 'v${OLDVERSION}'"
fi

if [ "x${OLDVERSION}" = "x" ]; then
 usage
fi

echo "updating from ${OLDVERSION}"

if [ "x${NEWVERSION}" = "x" ]; then
 usage
fi

if dpkg --compare-versions ${OLDVERSION} ge ${NEWVERSION}
then
 fatal "version mismatch: ${NEWVERSION} is not newer than ${OLDVERSION}"
fi

echo "updating to ${NEWVERSION}"

OK=false
mkdir debian
cp ${CHANGELOG} debian/changelog
gbp dch -R --since "v${OLDVERSION}" -N "${NEWVERSION}" --debian-branch="${mainbranch}" && cat debian/changelog > ${CHANGELOG} && OK=true
rm -rf debian

if [ "x${OK}" = "xtrue" ]; then
  sed -e "s|^PACKAGE_VERSION=\".*\"$|PACKAGE_VERSION=\"${NEWVERSION}\"|" -i dkms.conf
fi

if [ "x${OK}" = "xtrue" ]; then
 echo "all went well"
 echo ""
 echo "- please check your ${CHANGELOG}"
 echo "- please check&edit your ${NEWS}"
 echo "- please check&edit your ${AUTHORS}"
 echo "- and don't forget to git-tag the new version as v${NEWVERSION}"
 echo " git tag v${NEWVERSION} -s -m \"Released ${NEWVERSION}\""
fi
