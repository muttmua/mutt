#!/bin/sh

# Switch to directory where this script lives so that further commands are run
# from the root directory of the source.  The script path and srcdir are double
# quoted to allow the space character to appear in the path.
srcdir=`dirname "$0"` && cd "$srcdir" || exit 1

# Ensure that we have a repo here.
# If not, just cat the VERSION file; it contains the latest release number.
[ -d ".git" ] || exec cat VERSION

latesttag="$(git tag --merged=HEAD --list 'mutt-*-rel' | tr - . | sort -Vr | head -n1 | tr . -)"
version="$(echo $latesttag | sed -e s/mutt-// -e s/-rel// -e s/-/./g)"
distance="$(git rev-list --count $latesttag..)"
commitid="$(git rev-parse --short HEAD)"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  dirty=+
else
  dirty=""
fi
echo "${version}+${distance} (g${commitid}${dirty})"
