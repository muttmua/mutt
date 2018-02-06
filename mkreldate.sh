#!/bin/sh
#
# Generates the reldate.h contents from either git or the ChangeLog file

if [ -e ".git" ] && command -v git >/dev/null 2>&1; then
  reldate=$(git log -1 --date=short --pretty=format:"%cd")
else
  reldate=$(head -n 1 ChangeLog | LC_ALL=C cut -d ' ' -f 1)
fi

echo 'const char *ReleaseDate = "'$reldate'";'
