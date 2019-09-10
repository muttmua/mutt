/*
 * Copyright (C) 2018 Gero Treuner <gero@70t.de>
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

#ifndef MONITOR_H
#define MONITOR_H

WHERE int MonitorFilesChanged;
WHERE int MonitorContextChanged;

#ifdef _BUFFY_H
int mutt_monitor_add (BUFFY *b);
int mutt_monitor_remove (BUFFY *b);
#endif
int mutt_monitor_poll (void);

#endif /* MONITOR_H */
