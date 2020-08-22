#!/bin/sh
#
# Copyright (C) 2020 Eike Rathke <erack@erack.de>
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
# To conveniently switch between graphics and terminal editor I have the
# following in my muttrc:
# source '~/.mutt/bgedit-detectgui.sh|'
#
# So exporting MUTT_USE_GVIM=yes (or anything) in the shell invoking mutt
# switches to the background editing feature.
#

if [ -n "$MUTT_USE_GVIM" ] && [ -n "$DISPLAY" ]; then
    # Foreground gvim, window 80 cols by 40 rows at X 400 and Y 0.
    echo 'set editor="gvim -f -geometry 80x40+400+0"'
    echo 'set background_edit=yes'
else
    echo 'set editor=vim'
fi

