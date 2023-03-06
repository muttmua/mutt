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

static char MsgIdPfx = 'A';

typedef struct msg_id_data
{
  time_t now;
  struct tm tm;
  const char *fqdn;
} MSG_ID_DATA;

static const char *id_format_str (char *dest, size_t destlen, size_t col,
                                  int cols, char op, const char *src,
                                  const char *fmt, const char *ifstring,
                                  const char *elsestring,
                                  void *data, format_flag flags)
{
  MSG_ID_DATA *id_data = (MSG_ID_DATA *)data;
  char tmp[STRING];
  unsigned char r_raw[3];
  unsigned char r_out[4 + 1];
  unsigned char z_raw[12]; /* 32 bit timestamp, plus 64 bit randomness */
  unsigned char z_out[16 + 1];

  switch (op)
  {
    case 'r':
      mutt_random_bytes ((char *)r_raw, sizeof(r_raw));
      mutt_to_base64_safeurl (r_out, r_raw, sizeof(r_raw), sizeof(r_out));
      mutt_format_s (dest, destlen, fmt, (const char *)r_out);
      break;

    case 'x':
      /* hex encoded random byte */
      mutt_random_bytes ((char *)r_raw, sizeof(r_raw[0]));
      snprintf (dest, destlen, "%02x", r_raw[0]);
      break;

    case 'z':
      /* Convert the four least significant bytes of our timestamp and put it in
         localpart, with proper endianness (for humans) taken into account. */
      for (int i = 0; i < 4; i++)
        z_raw[i] = (uint8_t) (id_data->now >> (3-i)*8u);
      mutt_random_bytes ((char *)z_raw + 4, sizeof(z_raw) - 4);
      mutt_to_base64_safeurl (z_out, z_raw, sizeof(z_raw), sizeof(z_out));
      mutt_format_s (dest, destlen, fmt, (const char *)z_out);
      break;

    case 'Y':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, id_data->tm.tm_year + 1900);
      break;
    case 'm':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, id_data->tm.tm_mon + 1);
      break;
    case 'd':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, id_data->tm.tm_mday);
      break;
    case 'H':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, id_data->tm.tm_hour);
      break;
    case 'M':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, id_data->tm.tm_min);
      break;
    case 'S':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, id_data->tm.tm_sec);
      break;

    case 'c':
      snprintf (dest, destlen, "%c", MsgIdPfx);
      MsgIdPfx = (MsgIdPfx == 'Z') ? 'A' : MsgIdPfx + 1;
      break;

    case 'p':
      snprintf (tmp, sizeof (tmp), "%%%su", fmt);
      snprintf (dest, destlen, tmp, (unsigned int)getpid ());
      break;

    case 'f':
      mutt_format_s (dest, destlen, fmt, id_data->fqdn);
      break;
  }

  return (src);
}

char *mutt_gen_msgid (void)
{
  MSG_ID_DATA id_data;
  BUFFER *buf, *tmp;
  const char *fmt;
  char *rv;

  id_data.now = time (NULL);
  memcpy (&id_data.tm, gmtime (&id_data.now), sizeof(id_data.tm));
  if (!(id_data.fqdn = mutt_fqdn(0)))
    id_data.fqdn = NONULL(Hostname);

  fmt = MessageIdFormat;
  if (!fmt)
    fmt = "<%z@%f>";

  buf = mutt_buffer_pool_get ();
  mutt_FormatString (buf->data, buf->dsize, 0, buf->dsize,
                     fmt, id_format_str, &id_data, 0);
  mutt_buffer_fix_dptr (buf);

  /* this is hardly a thorough check, but at least make sure
   * we have the angle brackets. */
  if (!mutt_buffer_len (buf) ||
      (*buf->data != '<') || (*(buf->dptr - 1) != '>'))
  {
    tmp = mutt_buffer_pool_get ();
    if (!mutt_buffer_len (buf) || *buf->data != '<')
      mutt_buffer_addch (tmp, '<');
    mutt_buffer_addstr (tmp, mutt_b2s (buf));
    if (!mutt_buffer_len (buf) || *(buf->dptr - 1) != '>')
      mutt_buffer_addch (tmp, '>');
    mutt_buffer_strcpy (buf, mutt_b2s (tmp));
    mutt_buffer_pool_release (&tmp);
  }

  rv = safe_strdup (mutt_b2s (buf));
  mutt_buffer_pool_release (&buf);
  return rv;
}
