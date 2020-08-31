/*
 * Copyright (C) 2020 Remco Rĳnders <remco@webconquest.com>
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

#ifndef MUTT_RANDOM_H
#define MUTT_RANDOM_H
typedef union random64
{
  char     char_array[8];
  uint64_t int_64;
} RANDOM64;

void mutt_base64_random96(char output_B64[static 17]);
void mutt_random_bytes(char *random_bytes, int length_requested);
void mutt_reseed(void);
#endif
