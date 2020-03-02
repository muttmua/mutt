/*
 * Copyright (C) 2020 Kevin J. McCarthy <kevin@8t8.us>
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

#ifndef _BACKGROUND_H
#define _BACKGROUND_H 1

WHERE int BackgroundProcessCount;

int mutt_background_has_backgrounded (void);
int mutt_background_process_waitpid (void);
int mutt_background_edit_file (SEND_CONTEXT *sctx, const char *editor,
                               const char *filename);
void mutt_background_compose_menu (void);

#endif
