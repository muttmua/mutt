/*
 * Copyright (C) 2000 Edmund Grimley Evans <edmundo@rano.org>
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

/*
 * Japanese support by TAKIZAWA Takashi <taki@luna.email.ne.jp>.
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "mbyte.h"
#include "charset.h"

#include <errno.h>

#include <ctype.h>

#ifndef EILSEQ
#define EILSEQ EINVAL
#endif

int Charset_is_utf8 = 0;
#ifndef HAVE_WCWIDTH
static int charset_is_ja = 0;
#endif

void mutt_set_charset(char *charset)
{
  char buffer[STRING];

  mutt_canonical_charset(buffer, sizeof(buffer), charset);

  Charset_is_utf8 = 0;
#ifndef HAVE_WCWIDTH
  charset_is_ja = 0;
#endif

  if (mutt_is_utf8(buffer))
    Charset_is_utf8 = 1;
#ifndef HAVE_WCWIDTH
  else if (!ascii_strcasecmp(buffer, "euc-jp") || !ascii_strcasecmp(buffer, "shift_jis")
           || !ascii_strcasecmp(buffer, "cp932") || !ascii_strcasecmp(buffer, "eucJP-ms"))
  {
    charset_is_ja = 1;
  }
#endif

#if defined(HAVE_BIND_TEXTDOMAIN_CODESET) && defined(ENABLE_NLS)
  bind_textdomain_codeset(PACKAGE, buffer);
#endif
}

#ifndef HAVE_WCWIDTH
/*
 * l10n for Japanese:
 *   Symbols, Greek and Cyrillic in JIS X 0208, Japanese Kanji
 *   Character Set, have a column width of 2.
 */
static int wcwidth_ja(wchar_t ucs)
{
  if (ucs >= 0x3021)
    return -1; /* continue with the normal check */
  /* a rough range for quick check */
  if ((ucs >= 0x00a1 && ucs <= 0x00fe) || /* Latin-1 Supplement */
      (ucs >= 0x0391 && ucs <= 0x0451) || /* Greek and Cyrillic */
      (ucs >= 0x2010 && ucs <= 0x266f) || /* Symbols */
      (ucs >= 0x3000 && ucs <= 0x3020))   /* CJK Symbols and Punctuation */
    return 2;
  else
    return -1;
}

int wcwidth_ucs(wchar_t ucs);

int wcwidth(wchar_t wc)
{
  if (!Charset_is_utf8)
  {
    if (!charset_is_ja)
    {
      /* 8-bit case */
      if (!wc)
        return 0;
      else if ((0 <= wc && wc < 256) && IsPrint(wc))
        return 1;
      else
        return -1;
    }
    else
    {
      /* Japanese */
      int k = wcwidth_ja(wc);
      if (k != -1)
        return k;
    }
  }
  return wcwidth_ucs(wc);
}
#endif /* !HAVE_WCWIDTH */

wchar_t replacement_char(void)
{
  return Charset_is_utf8 ? 0xfffd : '?';
}

int is_display_corrupting_utf8(wchar_t wc)
{
  if (wc == (wchar_t)0x200f ||   /* bidi markers: #3827 */
      wc == (wchar_t)0x200e ||
      wc == (wchar_t)0x00ad ||   /* soft hyphen: #3848 */
      wc == (wchar_t)0xfeff ||   /* zero width no-break space */
      (wc >= (wchar_t)0x2066 &&  /* misc directional markers */
       wc <= (wchar_t)0x2069) ||
      (wc >= (wchar_t)0x202a &&  /* misc directional markers: #3854 */
       wc <= (wchar_t)0x202e) ||
      wc == (wchar_t)0x061c)     /* arabic letter mark: gitlab #413 */
    return 1;
  else
    return 0;
}

int mutt_filter_unprintable(char **s)
{
  BUFFER *b = NULL;
  wchar_t wc;
  size_t k, k2;
  char scratch[MB_LEN_MAX + 1];
  char *p = *s;
  mbstate_t mbstate1, mbstate2;

  b = mutt_buffer_new();
  memset(&mbstate1, 0, sizeof(mbstate1));
  memset(&mbstate2, 0, sizeof(mbstate2));
  for (; (k = mbrtowc(&wc, p, MB_LEN_MAX, &mbstate1)); p += k)
  {
    if (k == (size_t)(-1) || k == (size_t)(-2))
    {
      k = 1;
      memset(&mbstate1, 0, sizeof(mbstate1));
      wc = replacement_char();
    }
    if (!IsWPrint(wc))
      wc = '?';
    else if (Charset_is_utf8 &&
             is_display_corrupting_utf8(wc))
      continue;
    k2 = wcrtomb(scratch, wc, &mbstate2);
    scratch[k2] = '\0';
    mutt_buffer_addstr(b, scratch);
  }
  FREE(s);  /* __FREE_CHECKED__ */
  *s = b->data ? b->data : safe_calloc(1, 1);
  FREE(&b);
  return 0;
}
