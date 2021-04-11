/*
 * Copyright (C) 2021 Kevin J. McCarthy <kevin@8t8.us>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _COLOR_H
#define _COLOR_H 1

typedef struct color_attr
{
  short pair;   /* parameter to init_pair().  NOT the retval of COLOR_PAIR() */
  int attrs;
} COLOR_ATTR;


COLOR_ATTR mutt_merge_colors (COLOR_ATTR source, COLOR_ATTR overlay);
void mutt_attrset_cursor (COLOR_ATTR source, COLOR_ATTR cursor);

#ifdef HAVE_COLOR
int mutt_alloc_color (int fg, int bg);
int mutt_alloc_ansi_color (int fg, int bg);
int mutt_alloc_overlay_color (int fg, int bg);
void mutt_free_all_ansi_colors (void);
#endif


#endif
