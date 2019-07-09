/*
 * Copyright (C) 2019 Kevin J. McCarthy <kevin@8t8.us>
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

#ifndef _AUTOCRYPT_H
#define _AUTOCRYPT_H 1

#include <sqlite3.h>

WHERE sqlite3 *AutocryptDB;

typedef struct
{
  char *email_addr;
  char *keyid;
  char *keydata;
  int prefer_encrypt;    /* 0 = nopref, 1 = mutual */
  int enabled;
} AUTOCRYPT_ACCOUNT;

int mutt_autocrypt_init (int);
void mutt_autocrypt_cleanup (void);

#endif
