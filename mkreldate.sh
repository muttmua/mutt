#!/bin/sh
#
# Generates the reldate.h contents from either git or the ChangeLog file

if [ -r ".git" ] && command -v git >/dev/null 2>&1; then
  reldate=`TZ=UTC git log -1 --date=format-local:"%F" --pretty=format:"%cd"`
else
  reldate=`head -n 1 ChangeLog | LC_ALL=C cut -d ' ' -f 1`
fi

echo $reldate
