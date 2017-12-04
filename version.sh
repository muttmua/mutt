#!/bin/sh

# Switch to directory where this script lives so that further commands are run
# from the root directory of the source.  The script path and srcdir are double
# quoted to allow the space character to appear in the path.
srcdir=`dirname "$0"` && cd "$srcdir" || exit 1

# Ensure that we have a repo here.
# If not, just cat the VERSION file; it contains the latest release number.
[ -d ".git" ] || exec cat VERSION

# translate release tags into ##.##.## notation
get_tag () {
	sed -e 's/mutt-//' -e 's/-rel.*//' | tr - .
}

get_dist_node() {
	sed -e 's/.*-rel-//' -e 's/-/ /'
}

describe=`git describe --tags --long --match 'mutt-*-rel' 2>/dev/null` || exec cat VERSION

tag=`echo $describe | get_tag`

set -- `echo $describe | get_dist_node`
dist="$1"
node="$2"

if [ $dist -eq 0 ]; then
	dist=
else
	dist="+$dist"
fi

echo "$tag$dist ($node)"
exit 0
