#!/bin/sh
#
# Copyright (C) 2020 Kevin J. McCarthy <kevin@8t8.us>
#
#     This program is free software; you can redistribute it and/or modify
#     it under the terms of the GNU General Public License as published by
#     the Free Software Foundation; either version 2 of the License, or
#     (at your option) any later version.
#
#     This program is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#     GNU General Public License for more details.
#
#     You should have received a copy of the GNU General Public License
#     along with this program; if not, write to the Free Software
#     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# Invoke a background edit session in a new GNU Screen or tmux window.
#
# This script is derived from Aaron Schrab's tmuxwait script, posted to
# mutt-dev at
# <http://lists.mutt.org/pipermail/mutt-dev/Week-of-Mon-20200406/000591.html>.
#
# If you run mutt inside screen or tmux, add to your muttrc:
#   set background_edit
#   set editor = '/path/to/bgedit-screen-tmux.sh [youreditor]'
#
# It may also be useful to modify something like contrib/bgedit-detectgui.sh
# to look for the $STY or $TMUX environment variables and set those
# configuration variables appropriately.
#

set -e

if [ "$#" -lt 2 ]; then
  echo "Usage: $0 editor tempfile" >&2
  exit 1
fi

editor=$1
shift

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT INT QUIT
mkfifo "$tmpdir/status"

cat >"$tmpdir/run" <<END_SCRIPT
exitval=1
trap 'echo \$exitval > "$tmpdir/status"' EXIT INT QUIT
$editor "\$@"
exitval=\$?
END_SCRIPT

if test x$STY != x; then
  screen -X screen /bin/sh "$tmpdir/run" "$@"
elif test x$TMUX != x; then
  tmux neww /bin/sh "$tmpdir/run" "$@"
else
  echo "Not running inside a terminal emulator" >&2
  exit 1
fi

read exitval <"$tmpdir/status"
exit "$exitval"

