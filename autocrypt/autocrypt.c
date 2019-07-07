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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "autocrypt.h"
#include "autocrypt_private.h"

#include <errno.h>

static int autocrypt_dir_init (int can_create)
{
  int rv = 0;
  struct stat sb;
  BUFFER *prompt = NULL;

  if (!stat (AutocryptDir, &sb))
    return 0;

  if (!can_create)
    return -1;

  prompt = mutt_buffer_pool_get ();
  mutt_buffer_printf (prompt, _("%s does not exist. Create it?"), AutocryptDir);
  if (mutt_yesorno (mutt_b2s (prompt), MUTT_YES) == MUTT_YES)
  {
    if (mutt_mkdir (AutocryptDir, 0700) < 0)
    {
      mutt_error ( _("Can't create %s: %s."), AutocryptDir, strerror (errno));
      mutt_sleep (0);
      rv = -1;
    }
  }

  mutt_buffer_pool_release (&prompt);
  return rv;
}

int mutt_autocrypt_init (int can_create)
{
  if (AutocryptDB)
    return 0;

  if (!option (OPTAUTOCRYPT) || !AutocryptDir)
    return -1;

  if (autocrypt_dir_init (can_create))
    goto bail;

  if (mutt_autocrypt_gpgme_init ())
    goto bail;

  if (mutt_autocrypt_db_init (can_create))
    goto bail;

  return 0;

bail:
  unset_option (OPTAUTOCRYPT);
  mutt_autocrypt_db_close ();
  return -1;
}

void mutt_autocrypt_cleanup (void)
{
  mutt_autocrypt_db_close ();
}
