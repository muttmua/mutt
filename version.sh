#!/bin/sh

# Switch to directory where this script lives so that further commands are run
# from the root directory of the source.  The script path and srcdir are double
# quoted to allow the space character to appear in the path.
srcdir=`dirname "$0"` && cd "$srcdir" || exit 1

# Ensure that we have a repo here.
# If not, just cat the VERSION file; it contains the latest release number.
{ [ -r ".git" ] && command -v git >/dev/null 2>&1; } \
|| exec cat VERSION

latesttag=`git describe --tags --match 'mutt-*-rel' --abbrev=0`
version=`echo $latesttag | sed -e s/mutt-// -e s/-rel// -e s/-/./g`
distance=`git rev-list --count $latesttag..`
commitid=`git rev-parse --short HEAD`

[ x = "x$distance" ] && exec cat VERSION

if [ 0 -eq "$distance" ]; then
  distance=
else
  distance="+$distance"
fi

echo "${version}${distance} (${commitid})"
