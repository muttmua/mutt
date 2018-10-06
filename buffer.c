/*
 * Copyright (C) 1996-2002,2010,2013 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 2004 g10 Code GmbH
 * Copyright (C) 2018 Kevin J. McCarthy <kevin@8t8.us>
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
#include "buffer.h"

/* creates and initializes a BUFFER */
BUFFER *mutt_buffer_new(void) {
  BUFFER *b;

  b = safe_malloc(sizeof(BUFFER));

  mutt_buffer_init(b);

  return b;
}

/* initialize a new BUFFER */
BUFFER *mutt_buffer_init (BUFFER *b) {
  memset(b, 0, sizeof(BUFFER));
  return b;
}

/* Increases the allocated size of the buffer */
void mutt_buffer_increase_size (BUFFER *buf, size_t new_size)
{
  size_t offset;

  if (buf->dsize < new_size)
  {
    offset = buf->dptr - buf->data;
    buf->dsize = new_size;
    safe_realloc (&buf->data, buf->dsize);
    buf->dptr = buf->data + offset;
  }
}

/*
 * Creates and initializes a BUFFER*. If passed an existing BUFFER*,
 * just initializes. Frees anything already in the buffer. Copies in
 * the seed string.
 *
 * Disregards the 'destroy' flag, which seems reserved for caller.
 * This is bad, but there's no apparent protocol for it.
 */
BUFFER *mutt_buffer_from (char *seed) {
  BUFFER *b;

  if (!seed)
    return NULL;

  b = mutt_buffer_new ();
  b->data = safe_strdup(seed);
  b->dsize = mutt_strlen(seed);
  b->dptr = (char *) b->data + b->dsize;
  return b;
}

int mutt_buffer_printf (BUFFER* buf, const char* fmt, ...)
{
  va_list ap, ap_retry;
  int len, blen, doff;
  
  va_start (ap, fmt);
  va_copy (ap_retry, ap);

  if (!buf->dptr)
    buf->dptr = buf->data;

  doff = buf->dptr - buf->data;
  blen = buf->dsize - doff;
  /* solaris 9 vsnprintf barfs when blen is 0 */
  if (!blen)
  {
    blen = 128;
    buf->dsize += blen;
    safe_realloc (&buf->data, buf->dsize);
    buf->dptr = buf->data + doff;
  }
  if ((len = vsnprintf (buf->dptr, blen, fmt, ap)) >= blen)
  {
    blen = ++len - blen;
    if (blen < 128)
      blen = 128;
    buf->dsize += blen;
    safe_realloc (&buf->data, buf->dsize);
    buf->dptr = buf->data + doff;
    len = vsnprintf (buf->dptr, len, fmt, ap_retry);
  }
  if (len > 0)
    buf->dptr += len;

  va_end (ap);
  va_end (ap_retry);

  return len;
}

void mutt_buffer_addstr (BUFFER* buf, const char* s)
{
  mutt_buffer_add (buf, s, mutt_strlen (s));
}

void mutt_buffer_addch (BUFFER* buf, char c)
{
  mutt_buffer_add (buf, &c, 1);
}

void mutt_buffer_free (BUFFER **p)
{
  if (!p || !*p) 
    return;

   FREE(&(*p)->data);
   /* dptr is just an offset to data and shouldn't be freed */
   FREE(p);		/* __FREE_CHECKED__ */
}

/* dynamically grows a BUFFER to accommodate s, in increments of 128 bytes.
 * Always one byte bigger than necessary for the null terminator, and
 * the buffer is always null-terminated */
void mutt_buffer_add (BUFFER* buf, const char* s, size_t len)
{
  size_t offset;

  if (buf->dptr + len + 1 > buf->data + buf->dsize)
  {
    offset = buf->dptr - buf->data;
    buf->dsize += len < 128 ? 128 : len + 1;
    /* suppress compiler aliasing warning */
    safe_realloc ((void**) (void*) &buf->data, buf->dsize);
    buf->dptr = buf->data + offset;
  }
  memcpy (buf->dptr, s, len);
  buf->dptr += len;
  *(buf->dptr) = '\0';
}
