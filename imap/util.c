/*
 * Copyright (C) 1996-1998,2010,2012-2013 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 1996-1999 Brandon Long <blong@fiction.net>
 * Copyright (C) 1999-2009,2012 Brendan Cully <brendan@kublai.com>
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

/* general IMAP utility functions */

#include "config.h"

#include "mutt.h"
#include "mx.h"	/* for MUTT_IMAP */
#include "url.h"
#include "imap_private.h"
#ifdef USE_HCACHE
#include "hcache.h"
#endif

#include <stdlib.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#include <netinet/in.h>

#include <errno.h>

/* -- public functions -- */

/* imap_expand_path: IMAP implementation of mutt_expand_path. Rewrite
 *   an IMAP path in canonical and absolute form.
 * Inputs: a buffer containing an IMAP path.
 * Outputs: The buffer is rewritten in place with the canonical IMAP path.
 * Returns 0 on success, or -1 if imap_parse_path chokes or url_ciss_tobuffer
 *   fails, which it might if there isn't enough room in the buffer. */
int imap_expand_path (BUFFER* path)
{
  IMAP_MBOX mx;
  IMAP_DATA* idata;
  ciss_url_t url;
  char fixedpath[LONG_STRING];
  int rc;

  if (imap_parse_path (mutt_b2s (path), &mx) < 0)
    return -1;

  idata = imap_conn_find (&mx.account, MUTT_IMAP_CONN_NONEW);
  mutt_account_tourl (&mx.account, &url);
  imap_fix_path (idata, mx.mbox, fixedpath, sizeof (fixedpath));
  url.path = fixedpath;

  rc = url_ciss_tobuffer (&url, path, U_DECODE_PASSWD);
  FREE (&mx.mbox);

  return rc;
}

#ifdef USE_HCACHE

/* Generates a seqseq of the UIDs in msn_index to persist in the header cache.
 *
 * Empty spots are stored as 0.
 */
static void imap_msn_index_to_uid_seqset (BUFFER *b, IMAP_DATA *idata)
{
  int first = 1, state = 0, match = 0;
  HEADER *cur_header;
  unsigned int msn, cur_uid = 0, last_uid = 0;
  unsigned int range_begin = 0, range_end = 0;

  for (msn = 1; msn <= idata->max_msn + 1; msn++)
  {
    match = 0;
    if (msn <= idata->max_msn)
    {
      cur_header = idata->msn_index[msn - 1];
      cur_uid = cur_header ? HEADER_DATA(cur_header)->uid : 0;
      if (!state || (cur_uid && (cur_uid - 1 == last_uid)))
        match = 1;
      last_uid = cur_uid;
    }

    if (match)
    {
      switch (state)
      {
        case 1:            /* single: convert to a range */
          state = 2;
          /* fall through */
        case 2:            /* extend range ending */
          range_end = cur_uid;
          break;
        default:
          state = 1;
          range_begin = cur_uid;
          break;
      }
    }
    else if (state)
    {
      if (first)
        first = 0;
      else
        mutt_buffer_addch (b, ',');

      if (state == 1)
        mutt_buffer_add_printf (b, "%u", range_begin);
      else if (state == 2)
        mutt_buffer_add_printf (b, "%u:%u", range_begin, range_end);

      state = 1;
      range_begin = cur_uid;
    }
  }
}

static void imap_hcache_namer (const char *path, BUFFER *dest)
{
  mutt_buffer_printf (dest, "%s.hcache", path);
}

header_cache_t* imap_hcache_open (IMAP_DATA* idata, const char* path)
{
  IMAP_MBOX mx;
  ciss_url_t url;
  BUFFER *cachepath = NULL;
  BUFFER *mbox = NULL;
  size_t len;
  header_cache_t *rv = NULL;

  mbox = mutt_buffer_pool_get ();
  cachepath = mutt_buffer_pool_get ();

  if (path)
    imap_cachepath (idata, path, mbox);
  else
  {
    if (!idata->ctx || imap_parse_path (idata->ctx->path, &mx) < 0)
      goto cleanup;

    imap_cachepath (idata, mx.mbox, mbox);
    FREE (&mx.mbox);
  }

  if (strstr (mutt_b2s (mbox), "/../") ||
      (strcmp (mutt_b2s (mbox), "..") == 0) ||
      (strncmp(mutt_b2s (mbox), "../", 3) == 0))
    goto cleanup;
  len = mutt_buffer_len (mbox);
  if ((len > 3) && (strcmp(mutt_b2s (mbox) + len - 3, "/..") == 0))
    goto cleanup;

  mutt_account_tourl (&idata->conn->account, &url);
  url.path = mbox->data;
  url_ciss_tobuffer (&url, cachepath, U_PATH);

  rv = mutt_hcache_open (HeaderCache, mutt_b2s (cachepath), imap_hcache_namer);

cleanup:
  mutt_buffer_pool_release (&mbox);
  mutt_buffer_pool_release (&cachepath);
  return rv;
}

void imap_hcache_close (IMAP_DATA* idata)
{
  if (!idata->hcache)
    return;

  mutt_hcache_close (idata->hcache);
  idata->hcache = NULL;
}

HEADER* imap_hcache_get (IMAP_DATA* idata, unsigned int uid)
{
  char key[16];
  void *data;
  unsigned int uv;
  HEADER* h = NULL;

  if (!idata->hcache)
    return NULL;

  sprintf (key, "/%u", uid);
  data = mutt_hcache_fetch (idata->hcache, key,
                            imap_hcache_keylen);
  if (data)
  {
    memcpy (&uv, data, sizeof(unsigned int));
    if (uv == idata->uid_validity)
      h = mutt_hcache_restore ((unsigned char *)data, NULL);
    else
      dprint (3, (debugfile, "hcache uidvalidity mismatch: %u", uv));
    mutt_hcache_free ((void **)&data);
  }

  return h;
}

int imap_hcache_put (IMAP_DATA* idata, HEADER* h)
{
  char key[16];

  if (!idata->hcache)
    return -1;

  sprintf (key, "/%u", HEADER_DATA (h)->uid);
  return mutt_hcache_store (idata->hcache, key, h, idata->uid_validity,
                            imap_hcache_keylen, 0);
}

int imap_hcache_del (IMAP_DATA* idata, unsigned int uid)
{
  char key[16];

  if (!idata->hcache)
    return -1;

  sprintf (key, "/%u", uid);
  return mutt_hcache_delete (idata->hcache, key, imap_hcache_keylen);
}

int imap_hcache_store_uid_seqset (IMAP_DATA *idata)
{
  BUFFER *b;
  int rc;

  if (!idata->hcache)
    return -1;

  b = mutt_buffer_new ();
  /* The seqset is likely large.  Preallocate to reduce reallocs */
  mutt_buffer_increase_size (b, HUGE_STRING);
  imap_msn_index_to_uid_seqset (b, idata);

  rc = mutt_hcache_store_raw (idata->hcache, "/UIDSEQSET",
                              b->data, mutt_buffer_len (b) + 1,
                              imap_hcache_keylen);
  dprint (5, (debugfile, "Stored /UIDSEQSET %s\n", b->data));
  mutt_buffer_free (&b);
  return rc;
}

int imap_hcache_clear_uid_seqset (IMAP_DATA *idata)
{
  if (!idata->hcache)
    return -1;

  return mutt_hcache_delete (idata->hcache, "/UIDSEQSET", imap_hcache_keylen);
}

char *imap_hcache_get_uid_seqset (IMAP_DATA *idata)
{
  char *hc_seqset, *seqset;

  if (!idata->hcache)
    return NULL;

  hc_seqset = mutt_hcache_fetch_raw (idata->hcache, "/UIDSEQSET",
                                     imap_hcache_keylen);
  seqset = safe_strdup (hc_seqset);
  mutt_hcache_free ((void **)&hc_seqset);
  dprint (5, (debugfile, "Retrieved /UIDSEQSET %s\n", NONULL (seqset)));

  return seqset;
}
#endif

/* imap_parse_path: given an IMAP mailbox name, return host, port
 *   and a path IMAP servers will recognize.
 * mx.mbox is malloc'd, caller must free it */
int imap_parse_path (const char* path, IMAP_MBOX* mx)
{
  static unsigned short ImapPort = 0;
  static unsigned short ImapsPort = 0;
  struct servent* service;
  char tmp[128];
  ciss_url_t url;
  char *c;
  int n;

  if (!ImapPort)
  {
    service = getservbyname ("imap", "tcp");
    if (service)
      ImapPort = ntohs (service->s_port);
    else
      ImapPort = IMAP_PORT;
    dprint (3, (debugfile, "Using default IMAP port %d\n", ImapPort));
  }
  if (!ImapsPort)
  {
    service = getservbyname ("imaps", "tcp");
    if (service)
      ImapsPort = ntohs (service->s_port);
    else
      ImapsPort = IMAP_SSL_PORT;
    dprint (3, (debugfile, "Using default IMAPS port %d\n", ImapsPort));
  }

  /* Defaults */
  memset(&mx->account, 0, sizeof(mx->account));
  mx->account.port = ImapPort;
  mx->account.type = MUTT_ACCT_TYPE_IMAP;

  c = safe_strdup (path);
  url_parse_ciss (&url, c);
  if (url.scheme == U_IMAP || url.scheme == U_IMAPS)
  {
    if (mutt_account_fromurl (&mx->account, &url) < 0 || !*mx->account.host)
    {
      FREE (&c);
      return -1;
    }

    mx->mbox = safe_strdup (url.path);

    if (url.scheme == U_IMAPS)
      mx->account.flags |= MUTT_ACCT_SSL;

    FREE (&c);
  }
  /* old PINE-compatibility code */
  else
  {
    FREE (&c);
    if (sscanf (path, "{%127[^}]}", tmp) != 1)
      return -1;

    c = strchr (path, '}');
    if (!c)
      return -1;
    else
      /* walk past closing '}' */
      mx->mbox = safe_strdup (c+1);

    if ((c = strrchr (tmp, '@')))
    {
      *c = '\0';
      strfcpy (mx->account.user, tmp, sizeof (mx->account.user));
      strfcpy (tmp, c+1, sizeof (tmp));
      mx->account.flags |= MUTT_ACCT_USER;
    }

    if ((n = sscanf (tmp, "%127[^:/]%127s", mx->account.host, tmp)) < 1)
    {
      dprint (1, (debugfile, "imap_parse_path: NULL host in %s\n", path));
      FREE (&mx->mbox);
      return -1;
    }

    if (n > 1)
    {
      if (sscanf (tmp, ":%hu%127s", &(mx->account.port), tmp) >= 1)
	mx->account.flags |= MUTT_ACCT_PORT;
      if (sscanf (tmp, "/%s", tmp) == 1)
      {
	if (!ascii_strncmp (tmp, "ssl", 3))
	  mx->account.flags |= MUTT_ACCT_SSL;
	else
	{
	  dprint (1, (debugfile, "imap_parse_path: Unknown connection type in %s\n", path));
	  FREE (&mx->mbox);
	  return -1;
	}
      }
    }
  }

  if ((mx->account.flags & MUTT_ACCT_SSL) && !(mx->account.flags & MUTT_ACCT_PORT))
    mx->account.port = ImapsPort;

  return 0;
}

/* silly helper for mailbox name string comparisons, because of INBOX */
int imap_mxcmp (const char* mx1, const char* mx2)
{
  char* b1;
  char* b2;
  int rc;

  if (!mx1 || !*mx1)
    mx1 = "INBOX";
  if (!mx2 || !*mx2)
    mx2 = "INBOX";
  if (!ascii_strcasecmp (mx1, "INBOX") && !ascii_strcasecmp (mx2, "INBOX"))
    return 0;

  b1 = safe_malloc (strlen (mx1) + 1);
  b2 = safe_malloc (strlen (mx2) + 1);

  imap_fix_path (NULL, mx1, b1, strlen (mx1) + 1);
  imap_fix_path (NULL, mx2, b2, strlen (mx2) + 1);

  rc = mutt_strcmp (b1, b2);
  FREE (&b1);
  FREE (&b2);

  return rc;
}

/* imap_pretty_mailbox: called by mutt_pretty_mailbox to make IMAP paths
 *   look nice. */
void imap_pretty_mailbox (char* path, size_t pathlen)
{
  IMAP_MBOX home, target;
  ciss_url_t url;
  char* delim;
  int tlen;
  int hlen = 0;
  char home_match = 0;

  if (imap_parse_path (path, &target) < 0)
    return;

  tlen = mutt_strlen (target.mbox);
  /* check whether we can do '=' substitution */
  if (mx_is_imap(Maildir) && !imap_parse_path (Maildir, &home))
  {
    hlen = mutt_strlen (home.mbox);
    if (tlen && mutt_account_match (&home.account, &target.account) &&
	!mutt_strncmp (home.mbox, target.mbox, hlen))
    {
      if (! hlen)
	home_match = 1;
      else if (ImapDelimChars)
	for (delim = ImapDelimChars; *delim != '\0'; delim++)
	  if (target.mbox[hlen] == *delim)
	    home_match = 1;
    }
    FREE (&home.mbox);
  }

  /* do the '=' substitution */
  if (home_match)
  {
    *path++ = '=';
    /* copy remaining path, skipping delimiter */
    if (! hlen)
      hlen = -1;
    memcpy (path, target.mbox + hlen + 1, tlen - hlen - 1);
    path[tlen - hlen - 1] = '\0';
  }
  else
  {
    mutt_account_tourl (&target.account, &url);
    url.path = target.mbox;
    url_ciss_tostring (&url, path, pathlen, 0);
  }

  FREE (&target.mbox);
}

/* -- library functions -- */

/* imap_continue: display a message and ask the user if she wants to
 *   go on. */
int imap_continue (const char* msg, const char* resp)
{
  imap_error (msg, resp);
  return mutt_yesorno (_("Continue?"), 0);
}

/* imap_error: show an error and abort */
void imap_error (const char *where, const char *msg)
{
  mutt_error ("%s [%s]\n", where, msg);
  mutt_sleep (2);
}

/* imap_new_idata: Allocate and initialise a new IMAP_DATA structure. */
IMAP_DATA* imap_new_idata (void)
{
  IMAP_DATA* idata = safe_calloc (1, sizeof (IMAP_DATA));

  idata->cmdbuf = mutt_buffer_new ();
  idata->cmdslots = ImapPipelineDepth + 2;
  idata->cmds = safe_calloc (idata->cmdslots, sizeof(*idata->cmds));

  return idata;
}

/* imap_free_idata: Release and clear storage in an IMAP_DATA structure. */
void imap_free_idata (IMAP_DATA** idata)
{
  if (!idata)
    return;

  FREE (&(*idata)->capstr);
  mutt_free_list (&(*idata)->flags);
  imap_mboxcache_free (*idata);
  mutt_buffer_free(&(*idata)->cmdbuf);
  FREE (&(*idata)->buf);
  mutt_bcache_close (&(*idata)->bcache);
  FREE (&(*idata)->cmds);
  FREE (idata);		/* __FREE_CHECKED__ */
}

/*
 * Fix up the imap path.  This is necessary because the rest of mutt
 * assumes a hierarchy delimiter of '/', which is not necessarily true
 * in IMAP.  Additionally, the filesystem converts multiple hierarchy
 * delimiters into a single one, ie "///" is equal to "/".  IMAP servers
 * are not required to do this.
 * Moreover, IMAP servers may dislike the path ending with the delimiter.
 */
char *imap_fix_path (IMAP_DATA *idata, const char *mailbox, char *path,
                     size_t plen)
{
  int i = 0;
  char delim = '\0';

  if (idata)
    delim = idata->delim;

  while (mailbox && *mailbox && i < plen - 1)
  {
    if ((ImapDelimChars && strchr(ImapDelimChars, *mailbox))
        || (delim && *mailbox == delim))
    {
      /* use connection delimiter if known. Otherwise use user delimiter */
      if (!idata)
        delim = *mailbox;

      while (*mailbox
	     && ((ImapDelimChars && strchr(ImapDelimChars, *mailbox))
	         || (delim && *mailbox == delim)))
        mailbox++;
      path[i] = delim;
    }
    else
    {
      path[i] = *mailbox;
      mailbox++;
    }
    i++;
  }
  if (i && path[--i] != delim)
    i++;
  path[i] = '\0';

  return path;
}

void imap_cachepath (IMAP_DATA *idata, const char *mailbox, BUFFER *dest)
{
  const char *p = mailbox;

  mutt_buffer_clear (dest);
  if (!p)
    return;

  while (*p)
  {
    if (*p == idata->delim)
    {
      mutt_buffer_addch (dest, '/');
      /* simple way to avoid collisions with UIDs */
      if (*(p + 1) >= '0' && *(p + 1) <= '9')
        mutt_buffer_addch (dest, '_');
    }
    else
      mutt_buffer_addch (dest, *p);
    p++;
  }
}

/* imap_get_literal_count: write number of bytes in an IMAP literal into
 *   bytes, return 0 on success, -1 on failure. */
int imap_get_literal_count(const char *buf, unsigned int *bytes)
{
  char *pc;
  char *pn;

  if (!buf || !(pc = strchr (buf, '{')))
    return -1;

  pc++;
  pn = pc;
  while (isdigit ((unsigned char) *pc))
    pc++;
  *pc = 0;
  if (mutt_atoui (pn, bytes) < 0)
    return -1;

  return 0;
}

/* imap_get_qualifier: in a tagged response, skip tag and status for
 *   the qualifier message. Used by imap_copy_message for TRYCREATE */
char* imap_get_qualifier (char* buf)
{
  char *s = buf;

  /* skip tag */
  s = imap_next_word (s);
  /* skip OK/NO/BAD response */
  s = imap_next_word (s);

  return s;
}

/* imap_next_word: return index into string where next IMAP word begins */
char *imap_next_word (char *s)
{
  int quoted = 0;

  while (*s)
  {
    if (*s == '\\')
    {
      s++;
      if (*s)
	s++;
      continue;
    }
    if (*s == '\"')
      quoted = quoted ? 0 : 1;
    if (!quoted && ISSPACE (*s))
      break;
    s++;
  }

  SKIPWS (s);
  return s;
}

/* imap_parse_date: date is of the form: DD-MMM-YYYY HH:MM:SS +ZZzz */
time_t imap_parse_date (char *s)
{
  struct tm t;
  time_t tz;

  t.tm_mday = (s[0] == ' '? s[1] - '0' : (s[0] - '0') * 10 + (s[1] - '0'));
  s += 2;
  if (*s != '-')
    return 0;
  s++;
  t.tm_mon = mutt_check_month (s);
  s += 3;
  if (*s != '-')
    return 0;
  s++;
  t.tm_year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0') - 1900;
  s += 4;
  if (*s != ' ')
    return 0;
  s++;

  /* time */
  t.tm_hour = (s[0] - '0') * 10 + (s[1] - '0');
  s += 2;
  if (*s != ':')
    return 0;
  s++;
  t.tm_min = (s[0] - '0') * 10 + (s[1] - '0');
  s += 2;
  if (*s != ':')
    return 0;
  s++;
  t.tm_sec = (s[0] - '0') * 10 + (s[1] - '0');
  s += 2;
  if (*s != ' ')
    return 0;
  s++;

  /* timezone */
  tz = ((s[1] - '0') * 10 + (s[2] - '0')) * 3600 +
    ((s[3] - '0') * 10 + (s[4] - '0')) * 60;
  if (s[0] == '+')
    tz = -tz;

  return (mutt_mktime (&t, 0) + tz);
}

/* format date in IMAP style: DD-MMM-YYYY HH:MM:SS +ZZzz.
 * Caller should provide a buffer of IMAP_DATELEN bytes */
void imap_make_date (char *buf, time_t timestamp)
{
  struct tm* tm = localtime (&timestamp);
  time_t tz = mutt_local_tz (timestamp);

  tz /= 60;

  snprintf (buf, IMAP_DATELEN, "%02d-%s-%d %02d:%02d:%02d %+03d%02d",
            tm->tm_mday, Months[tm->tm_mon], tm->tm_year + 1900,
            tm->tm_hour, tm->tm_min, tm->tm_sec,
            (int) tz / 60, (int) abs ((int) tz) % 60);
}

/* imap_qualify_path: make an absolute IMAP folder target, given IMAP_MBOX
 *   and relative path. */
void imap_qualify_path (char *dest, size_t len, IMAP_MBOX *mx, char* path)
{
  ciss_url_t url;

  mutt_account_tourl (&mx->account, &url);
  url.path = path;

  url_ciss_tostring (&url, dest, len, 0);
}

void imap_buffer_qualify_path (BUFFER *dest, IMAP_MBOX *mx, char* path)
{
  ciss_url_t url;

  mutt_account_tourl (&mx->account, &url);
  url.path = path;

  url_ciss_tobuffer (&url, dest, 0);
}


static void _imap_quote_string (char *dest, size_t dlen, const char *src,
                                const char *to_quote)
{
  char *pt;
  const char *s;

  if (!(dest && dlen && src && to_quote))
    return;

  if (dlen < 3)
  {
    *dest = 0;
    return;
  }

  pt = dest;
  s  = src;

  /* save room for pre/post quote-char and trailing null */
  dlen -= 3;

  *pt++ = '"';
  for (; *s && dlen; s++)
  {
    if (strchr (to_quote, *s))
    {
      if (dlen < 2)
        break;
      dlen -= 2;
      *pt++ = '\\';
      *pt++ = *s;
    }
    else
    {
      *pt++ = *s;
      dlen--;
    }
  }
  *pt++ = '"';
  *pt = 0;
}

/* imap_quote_string: quote string according to IMAP rules:
 *   surround string with quotes, escape " and \ with \ */
void imap_quote_string (char *dest, size_t dlen, const char *src)
{
  _imap_quote_string (dest, dlen, src, "\"\\");
}

/* imap_quote_string_and_backquotes: quote string according to IMAP rules:
 *   surround string with quotes, escape " and \ with \.
 * Additionally, escape backquotes with \ to protect against code injection
 * when using the resulting string in mutt_parse_rc_line().
 */
void imap_quote_string_and_backquotes (char *dest, size_t dlen, const char *src)
{
  _imap_quote_string (dest, dlen, src, "\"\\`");
}

/* imap_unquote_string: equally stupid unquoting routine */
void imap_unquote_string (char *s)
{
  char *d = s;

  if (*s == '\"')
    s++;
  else
    return;

  while (*s)
  {
    if (*s == '\"')
    {
      *d = '\0';
      return;
    }
    if (*s == '\\')
    {
      s++;
    }
    if (*s)
    {
      *d = *s;
      d++;
      s++;
    }
  }
  *d = '\0';
}


/*
 * Quoting and UTF-7 conversion
 */

void imap_munge_mbox_name (IMAP_DATA *idata, char *dest, size_t dlen, const char *src)
{
  char *buf;

  buf = safe_strdup (src);
  imap_utf_encode (idata, &buf);

  imap_quote_string (dest, dlen, buf);

  FREE (&buf);
}

void imap_unmunge_mbox_name (IMAP_DATA *idata, char *s)
{
  char *buf;

  imap_unquote_string(s);

  buf = safe_strdup (s);
  if (buf)
  {
    imap_utf_decode (idata, &buf);
    strncpy (s, buf, strlen (s));
  }

  FREE (&buf);
}

/* imap_wordcasecmp: find word a in word list b */
int imap_wordcasecmp(const char *a, const char *b)
{
  char tmp[SHORT_STRING];
  char *s = (char *)b;
  int i;

  tmp[SHORT_STRING-1] = 0;
  for (i=0;i < SHORT_STRING-2;i++,s++)
  {
    if (!*s || ISSPACE(*s))
    {
      tmp[i] = 0;
      break;
    }
    tmp[i] = *s;
  }
  tmp[i+1] = 0;

  return ascii_strcasecmp(a, tmp);
}

/*
 * Imap keepalive: poll the current folder to keep the
 * connection alive.
 *
 */

static void alrm_handler (int sig)
{
  /* empty */
}

void imap_keepalive (void)
{
  CONNECTION *conn;
  CONTEXT *ctx = NULL;
  IMAP_DATA *idata;

  conn = mutt_socket_head ();
  while (conn)
  {
    if (conn->account.type == MUTT_ACCT_TYPE_IMAP)
    {
      int need_free = 0;

      idata = (IMAP_DATA*) conn->data;

      if (idata->state >= IMAP_AUTHENTICATED
	  && time(NULL) >= idata->lastread + ImapKeepalive)
      {
	if (idata->ctx)
	  ctx = idata->ctx;
	else
	{
	  ctx = safe_calloc (1, sizeof (CONTEXT));
	  ctx->data = idata;
	  /* imap_close_mailbox will set ctx->iadata->ctx to NULL, so we can't
	   * rely on the value of iadata->ctx to determine if this placeholder
	   * context needs to be freed.
	   */
	  need_free = 1;
	}
	/* if the imap connection closes during this call, ctx may be invalid
	 * after this point, and thus should not be read.
	 */
	imap_check_mailbox (ctx, NULL, 1);
	if (need_free)
	  FREE (&ctx);
      }
    }

    conn = conn->next;
  }
}

int imap_wait_keepalive (pid_t pid)
{
  struct sigaction oldalrm;
  struct sigaction act;
  sigset_t oldmask;
  int rc;

  short imap_passive = option (OPTIMAPPASSIVE);

  set_option (OPTIMAPPASSIVE);
  set_option (OPTKEEPQUIET);

  sigprocmask (SIG_SETMASK, NULL, &oldmask);

  sigemptyset (&act.sa_mask);
  act.sa_handler = alrm_handler;
#ifdef SA_INTERRUPT
  act.sa_flags = SA_INTERRUPT;
#else
  act.sa_flags = 0;
#endif

  sigaction (SIGALRM, &act, &oldalrm);

  alarm (ImapKeepalive);
  while (waitpid (pid, &rc, 0) < 0 && errno == EINTR)
  {
    alarm (0); /* cancel a possibly pending alarm */
    imap_keepalive ();
    alarm (ImapKeepalive);
  }

  alarm (0);	/* cancel a possibly pending alarm */

  sigaction (SIGALRM, &oldalrm, NULL);
  sigprocmask (SIG_SETMASK, &oldmask, NULL);

  unset_option (OPTKEEPQUIET);
  if (!imap_passive)
    unset_option (OPTIMAPPASSIVE);

  return rc;
}

/* Allow/disallow re-opening a folder upon expunge. */

void imap_allow_reopen (CONTEXT *ctx)
{
  if (ctx && ctx->magic == MUTT_IMAP && CTX_DATA->ctx == ctx)
    CTX_DATA->reopen |= IMAP_REOPEN_ALLOW;
}

void imap_disallow_reopen (CONTEXT *ctx)
{
  if (ctx && ctx->magic == MUTT_IMAP && CTX_DATA->ctx == ctx)
    CTX_DATA->reopen &= ~IMAP_REOPEN_ALLOW;
}

int imap_account_match (const ACCOUNT* a1, const ACCOUNT* a2)
{
  IMAP_DATA* a1_idata = imap_conn_find (a1, MUTT_IMAP_CONN_NONEW);
  IMAP_DATA* a2_idata = imap_conn_find (a2, MUTT_IMAP_CONN_NONEW);
  const ACCOUNT* a1_canon = a1_idata == NULL ? a1 : &a1_idata->conn->account;
  const ACCOUNT* a2_canon = a2_idata == NULL ? a2 : &a2_idata->conn->account;

  return mutt_account_match (a1_canon, a2_canon);
}

/* Sequence set iteration */

SEQSET_ITERATOR *mutt_seqset_iterator_new (const char *seqset)
{
  SEQSET_ITERATOR *iter;

  if (!seqset || !*seqset)
    return NULL;

  iter = safe_calloc (1, sizeof(SEQSET_ITERATOR));
  iter->full_seqset = safe_strdup (seqset);
  iter->eostr = strchr (iter->full_seqset, '\0');
  iter->substr_cur = iter->substr_end = iter->full_seqset;

  return iter;
}

/* Returns: 0 when the next sequence is generated
 *          1 when the iterator is finished
 *         -1 on error
 */
int mutt_seqset_iterator_next (SEQSET_ITERATOR *iter, unsigned int *next)
{
  char *range_sep;

  if (!iter || !next)
    return -1;

  if (iter->in_range)
  {
    if ((iter->down && iter->range_cur == (iter->range_end - 1)) ||
        (!iter->down && iter->range_cur == (iter->range_end + 1)))
      iter->in_range = 0;
  }

  if (!iter->in_range)
  {
    iter->substr_cur = iter->substr_end;
    if (iter->substr_cur == iter->eostr)
      return 1;

    while (!*(iter->substr_cur))
      iter->substr_cur++;
    iter->substr_end = strchr (iter->substr_cur, ',');
    if (!iter->substr_end)
      iter->substr_end = iter->eostr;
    else
      *(iter->substr_end) = '\0';

    range_sep = strchr (iter->substr_cur, ':');
    if (range_sep)
      *range_sep++ = '\0';

    if (mutt_atoui (iter->substr_cur, &iter->range_cur))
      return -1;
    if (range_sep)
    {
      if (mutt_atoui (range_sep, &iter->range_end))
        return -1;
    }
    else
      iter->range_end = iter->range_cur;

    iter->down = (iter->range_end < iter->range_cur);
    iter->in_range = 1;
  }

  *next = iter->range_cur;
  if (iter->down)
    iter->range_cur--;
  else
    iter->range_cur++;

  return 0;
}

void mutt_seqset_iterator_free (SEQSET_ITERATOR **p_iter)
{
  SEQSET_ITERATOR *iter;

  if (!p_iter || !*p_iter)
    return;

  iter = *p_iter;
  FREE (&iter->full_seqset);
  FREE (p_iter);               /* __FREE_CHECKED__ */
}
