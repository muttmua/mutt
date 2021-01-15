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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "mutt_random.h"

char *mutt_gen_msgid (void)
{
  char buf[SHORT_STRING];
  time_t now = time (NULL);
  char random_bytes[8];
  char localpart[12]; /* = 32 bit timestamp, plus 64 bit randomness */
  unsigned char localpart_B64[16+1]; /* = Base64 encoded value of localpart plus
                                        terminating \0 */
  const char *fqdn;

  mutt_random_bytes (random_bytes, sizeof(random_bytes));

  /* Convert the four least significant bytes of our timestamp and put it in
     localpart, with proper endianness (for humans) taken into account. */
  for (int i = 0; i < 4; i++)
    localpart[i] = (uint8_t) (now >> (3-i)*8u);

  memcpy (&localpart[4], &random_bytes, 8);

  mutt_to_base64 (localpart_B64, (unsigned char *) localpart, 12, 17);

  if (!(fqdn = mutt_fqdn (0)))
    fqdn = NONULL (Hostname);

  snprintf (buf, sizeof (buf), "<%s@%s>", localpart_B64, fqdn);
  return (safe_strdup (buf));
}
