#!/bin/sh


getcurrentversion () {
  dpkg-parsechangelog --count 1 -lChangeLog |  egrep "^Version:" | head -1 | cut -f2 -d' '
}

if [ "x$1" = "x" ]; then
 echo "usage: $0 [<LASTVERSION>] <CURVERSION>" 1>&2
 exit 1
fi

if [ "x$2" = "x" ]; then
## guess current version
 NEWVERSION=$1
 OLDVERSION=$(getcurrentversion)
else
 OLDVERSION=$1
 NEWVERSION=$2
fi


if git tag -l v${OLDVERSION} | grep . >/dev/null
then
 :
else
 echo "it seems like there is no tag 'v${OLDVERSION}'" 1>&2
 exit 1
fi

if dpkg --compare-versions ${OLDVERSION} ge ${NEWVERSION}
then
 echo "version mismatch: $OLDVERSION is newer than $NEWVERSION" 1>&2
 exit 1
fi

echo "updating from $OLDVERSION to $NEWVERSION"

mkdir debian
cp ChangeLog debian/changelog
git-dch --since ${OLDVERSION} -N v${NEWVERSION}
cat debian/changelog > ChangeLog
rm -rf debian

echo "don't forget to git-tag the new version as v${NEWVERSION}"
