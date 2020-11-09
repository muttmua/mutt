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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "mutt_random.h"

#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static uint32_t z[4]; /* Keep state for LFRS113 PRNG */
static int rand_bytes_produced = 0;
static time_t time_last_reseed = 0;

void mutt_random_bytes (char *random_bytes, int length_requested)
{
  /* Reseed every day or after more than a 100000 random bytes produced */
  if (time (NULL) - time_last_reseed > 86400 || rand_bytes_produced > 100000)
    mutt_reseed ();

  uint32_t b;

  /* The loop below is our implementation of the LFRS113 PRNG algorithm by
   * Pierre L'Ecuyer */
  for (int i = length_requested; i > 0;)
  {
    b    = ((z[0] <<  6) ^ z[0]) >> 13;
    z[0] = ((z[0] & 4294967294U) << 18) ^ b;
    b    = ((z[1] <<  2) ^ z[1]) >> 27;
    z[1] = ((z[1] & 4294967288U) <<  2) ^ b;
    b    = ((z[2] << 13) ^ z[2]) >> 21;
    z[2] = ((z[2] & 4294967280U) <<  7) ^ b;
    b    = ((z[3] <<  3) ^ z[3]) >> 12;
    z[3] = ((z[3] & 4294967168U) << 13) ^ b;

    b    = z[0] ^ z[1] ^ z[2] ^ z[3];

    if (--i >= 0) random_bytes[i] = (b >> 24) & 0xFF;
    if (--i >= 0) random_bytes[i] = (b >> 16) & 0xFF;
    if (--i >= 0) random_bytes[i] = (b >>  8) & 0xFF;
    if (--i >= 0) random_bytes[i] = (b)       & 0xFF;
  }
  rand_bytes_produced += length_requested;

  return;
}

void mutt_reseed (void)
{
  uint32_t t[4];                /* Temp seed values from /dev/urandom */
  char computer_says_no = TRUE; /* Whether /dev/urandom was usable */

  /* Reset counters */
  rand_bytes_produced = 0;
  time_last_reseed = time (NULL);

  /* Try to seed from /dev/urandom, but don't cry if we fail */
  FILE *random_dev = NULL;

  if ((random_dev = fopen ("/dev/urandom", "r")))
  {
    if (fread (t, 1, sizeof(t), random_dev) > 0)
      computer_says_no = FALSE;
    safe_fclose (&random_dev);
  }

  /* Use current time as part of our seeding process */
  struct timeval tv;
  gettimeofday (&tv, NULL);
  /* POSIX.1-2008 states that seed is 'unsigned' without specifying its width.
   * Use as many of the lower order bits from the current time of day as the seed.
   * If the upper bound is truncated, that is fine.
   *
   * tv_sec is integral of type integer or float.  Cast to 'uint32_t' before
   * bitshift in case it is a float. */

  /* Finally, set our seeds */
  z[0] ^= (((uint32_t) tv.tv_sec << 20) | tv.tv_usec);
  z[1] ^= getpid ()                             ^ z[0];
  z[2] ^= getppid ()                            ^ z[0];
  z[3] ^= (intptr_t) &z[3] ^ time_last_reseed   ^ z[0];

  if (!computer_says_no) /* Mix in /dev/urandom values */
    for (int i = 0; i <= 3; i++)
      z[i] ^= t[i];
}

/* Generate and Base64 encode 96 random bits and fill the buffer
   output_B64 with the result. */
void mutt_base64_random96 (char output_B64[static 17])
{
  char random_bytes[12];

  mutt_random_bytes (random_bytes, sizeof(random_bytes));
  mutt_to_base64 ((unsigned char *) output_B64, (unsigned char *) random_bytes,
                  12, 17);
}
