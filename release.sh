#!/bin/sh
####################################
# changelog generator using git-dch

CHANGELOG=ChangeLog


getcurrentversion () {
  dpkg-parsechangelog --count 1 -l${CHANGELOG} |  egrep "^Version:" | head -1 | cut -f2 -d' '
}

if [ "x$2" = "x" ]; then
## guess current version
 NEWVERSION=$1
 OLDVERSION=$(getcurrentversion)
else
 OLDVERSION=$1
 NEWVERSION=$2
fi

if [ "x$NEWVERSION" = "x" ]; then
  NEWVERSION=$(./currentversion.sh)
fi

if git tag -l v${OLDVERSION} | grep . >/dev/null
then
 :
else
 echo "it seems like there is no tag 'v${OLDVERSION}'" 1>&2
 exit 1
fi

if [ "x$OLDVERSION" = "x" ]; then
 echo "usage: $0 [[<LASTVERSION>] <CURVERSION>]" 1>&2
 exit 1
fi

echo "updating from $OLDVERSION"

if [ "x$NEWVERSION" = "x" ]; then
 echo "usage: $0 [<LASTVERSION>] <CURVERSION>" 1>&2
 exit 1
fi

if dpkg --compare-versions ${OLDVERSION} ge ${NEWVERSION}
then
 echo "version mismatch: $OLDVERSION is newer than $NEWVERSION" 1>&2
 exit 1
fi

echo "updating to $NEWVERSION"

OK=false
mkdir debian 
cp ${CHANGELOG} debian/changelog
git-dch -R --since "v${OLDVERSION}" -N ${NEWVERSION} && cat debian/changelog > ${CHANGELOG} && OK=true
rm -rf debian

if [ "x$OK" = "xtrue" ]; then
  sed -e "s|^PACKAGE_VERSION=\".*\"$|PACKAGE_VERSION=\"${NEWVERSION}\"|" -i dkms.conf
fi

if [ "x$OK" = "xtrue" ]; then
 echo "all went well"
 echo "check your $CHANGELOG and don't forget to git-tag the new version as v${NEWVERSION}"
 echo " git tag v${NEWVERSION}"
fi
