#!/bin/sh

OLDVERSION=$1
NEWVERSION=$2

if [ "x${NEWVERSION}" = "x" ]; then
 echo "usage: $0 <LASTVERSION> <CURVERSION>" 1>&2
 exit 1
fi

mkdir debian
cp ChangeLog debian/changelog
git-dch --since ${OLDVERSION} -N ${NEWVERSION}
cat debian/changelog > ChangeLog
rm -rf debian
