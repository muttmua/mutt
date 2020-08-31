/*
 * Copyright (C) 1996-2000,2007,2010,2013 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 1999-2008 Thomas Roessler <roessler@does-not-exist.org>
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

#include "version.h"
#include "mutt.h"
#include "mutt_curses.h"
#include "mime.h"
#include "mailbox.h"
#include "mx.h"
#include "url.h"

#ifdef USE_IMAP
#include "imap.h"
#endif

#include "mutt_crypt.h"
#include "mutt_random.h"

#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <utime.h>
#include <dirent.h>

BODY *mutt_new_body (void)
{
  BODY *p = (BODY *) safe_calloc (1, sizeof (BODY));

  p->disposition = DISPATTACH;
  p->use_disp = 1;
  return (p);
}

/* Modified by blong to accept a "suggestion" for file name.  If
 * that file exists, then construct one with unique name but
 * keep any extension.  This might fail, I guess.
 * Renamed to mutt_adv_mktemp so I only have to change where it's
 * called, and not all possible cases.
 */
void mutt_adv_mktemp (BUFFER *buf)
{
  BUFFER *prefix = NULL;
  char *suffix;
  struct stat sb;

  if (!(buf->data && buf->data[0]))
  {
    mutt_buffer_mktemp (buf);
  }
  else
  {
    prefix = mutt_buffer_pool_get ();
    mutt_buffer_strcpy (prefix, buf->data);
    mutt_sanitize_filename (prefix->data, 1);
    mutt_buffer_printf (buf, "%s/%s", NONULL (Tempdir), mutt_b2s (prefix));
    if (lstat (mutt_b2s (buf), &sb) == -1 && errno == ENOENT)
      goto out;

    if ((suffix = strrchr (prefix->data, '.')) != NULL)
    {
      *suffix = 0;
      ++suffix;
    }
    mutt_buffer_mktemp_pfx_sfx (buf, mutt_b2s (prefix), suffix);

  out:
    mutt_buffer_pool_release (&prefix);
  }
}

/* create a send-mode duplicate from a receive-mode body */

int mutt_copy_body (FILE *fp, BODY **tgt, BODY *src)
{
  BUFFER *tmp = NULL;
  BODY *b;
  PARAMETER *par, **ppar;
  short use_disp;

  tmp = mutt_buffer_pool_get ();

  if (src->filename)
  {
    use_disp = 1;
    mutt_buffer_strcpy (tmp, src->filename);
  }
  else
  {
    use_disp = 0;
  }

  mutt_adv_mktemp (tmp);
  if (mutt_save_attachment (fp, src, mutt_b2s (tmp), 0, NULL) == -1)
  {
    mutt_buffer_pool_release (&tmp);
    return -1;
  }

  *tgt = mutt_new_body ();
  b = *tgt;

  memcpy (b, src, sizeof (BODY));
  b->parts = NULL;
  b->next  = NULL;

  b->filename = safe_strdup (mutt_b2s (tmp));
  b->use_disp = use_disp;
  b->unlink = 1;

  if (mutt_is_text_part (b))
    b->noconv = 1;

  b->xtype = safe_strdup (b->xtype);
  b->subtype = safe_strdup (b->subtype);
  b->form_name = safe_strdup (b->form_name);
  b->d_filename = safe_strdup (b->d_filename);
  /* mutt_adv_mktemp() will mangle the filename in tmp,
   * so preserve it in d_filename */
  if (!b->d_filename && use_disp)
    b->d_filename = safe_strdup (src->filename);
  b->description = safe_strdup (b->description);

  /*
   * we don't seem to need the HEADER structure currently.
   * XXX - this may change in the future
   */

  if (b->hdr) b->hdr = NULL;

  /* copy parameters */
  for (par = b->parameter, ppar = &b->parameter; par; ppar = &(*ppar)->next, par = par->next)
  {
    *ppar = mutt_new_parameter ();
    (*ppar)->attribute = safe_strdup (par->attribute);
    (*ppar)->value = safe_strdup (par->value);
  }

  mutt_stamp_attachment (b);

  mutt_buffer_pool_release (&tmp);
  return 0;
}



void mutt_free_body (BODY **p)
{
  BODY *a = *p, *b;

  while (a)
  {
    b = a;
    a = a->next;

    if (b->parameter)
      mutt_free_parameter (&b->parameter);
    if (b->filename)
    {
      if (b->unlink)
	unlink (b->filename);
      dprint (1, (debugfile, "mutt_free_body: %sunlinking %s.\n",
                  b->unlink ? "" : "not ", b->filename));
    }

    FREE (&b->filename);
    FREE (&b->d_filename);
    FREE (&b->charset);
    FREE (&b->content);
    FREE (&b->xtype);
    FREE (&b->subtype);
    FREE (&b->description);
    FREE (&b->form_name);

    if (b->hdr)
    {
      /* Don't free twice (b->hdr->content = b->parts) */
      b->hdr->content = NULL;
      mutt_free_header(&b->hdr);
    }

    mutt_free_envelope (&b->mime_headers);

    if (b->parts)
      mutt_free_body (&b->parts);

    FREE (&b);
  }

  *p = 0;
}

void mutt_free_parameter (PARAMETER **p)
{
  PARAMETER *t = *p;
  PARAMETER *o;

  while (t)
  {
    FREE (&t->attribute);
    FREE (&t->value);
    o = t;
    t = t->next;
    FREE (&o);
  }
  *p = 0;
}

LIST *mutt_add_list (LIST *head, const char *data)
{
  size_t len = mutt_strlen (data);

  return mutt_add_list_n (head, data, len ? len + 1 : 0);
}

LIST *mutt_add_list_n (LIST *head, const void *data, size_t len)
{
  LIST *tmp;

  for (tmp = head; tmp && tmp->next; tmp = tmp->next)
    ;
  if (tmp)
  {
    tmp->next = safe_malloc (sizeof (LIST));
    tmp = tmp->next;
  }
  else
    head = tmp = safe_malloc (sizeof (LIST));

  tmp->data = safe_malloc (len);
  if (len)
    memcpy (tmp->data, data, len);
  tmp->next = NULL;
  return head;
}

LIST *mutt_find_list (LIST *l, const char *data)
{
  LIST *p = l;

  while (p)
  {
    if (data == p->data)
      return p;
    if (data && p->data && mutt_strcmp (p->data, data) == 0)
      return p;
    p = p->next;
  }
  return NULL;
}

int mutt_remove_from_rx_list (RX_LIST **l, const char *str)
{
  RX_LIST *p, *last = NULL;
  int rv = -1;

  if (mutt_strcmp ("*", str) == 0)
  {
    mutt_free_rx_list (l);    /* ``unCMD *'' means delete all current entries */
    rv = 0;
  }
  else
  {
    p = *l;
    last = NULL;
    while (p)
    {
      if (ascii_strcasecmp (str, p->rx->pattern) == 0)
      {
	mutt_free_regexp (&p->rx);
	if (last)
	  last->next = p->next;
	else
	  (*l) = p->next;
	FREE (&p);
	rv = 0;
      }
      else
      {
	last = p;
	p = p->next;
      }
    }
  }
  return (rv);
}

void mutt_free_list (LIST **list)
{
  LIST *p;

  if (!list) return;
  while (*list)
  {
    p = *list;
    *list = (*list)->next;
    FREE (&p->data);
    FREE (&p);
  }
}

void mutt_free_list_generic(LIST **list, void (*data_free)(char **))
{
  LIST *p;

  /* wrap mutt_free_list if no data_free function was provided */
  if (data_free == NULL)
  {
    mutt_free_list(list);
    return;
  }

  if (!list) return;
  while (*list)
  {
    p = *list;
    *list = (*list)->next;
    data_free(&p->data);
    FREE (&p);
  }
}

LIST *mutt_copy_list (LIST *p)
{
  LIST *t, *r=NULL, *l=NULL;

  for (; p; p = p->next)
  {
    t = (LIST *) safe_malloc (sizeof (LIST));
    t->data = safe_strdup (p->data);
    t->next = NULL;
    if (l)
    {
      r->next = t;
      r = r->next;
    }
    else
      l = r = t;
  }
  return (l);
}

HEADER *mutt_dup_header(HEADER *h)
{
  HEADER *hnew;

  hnew = mutt_new_header();
  memcpy(hnew, h, sizeof (HEADER));
  return hnew;
}

void mutt_free_header (HEADER **h)
{
  if (!h || !*h) return;
  mutt_free_envelope (&(*h)->env);
  mutt_free_body (&(*h)->content);
  FREE (&(*h)->maildir_flags);
  FREE (&(*h)->tree);
  FREE (&(*h)->path);
#ifdef MIXMASTER
  mutt_free_list (&(*h)->chain);
#endif
#if defined USE_POP || defined USE_IMAP
  FREE (&(*h)->data);
#endif
  FREE (h);		/* __FREE_CHECKED__ */
}

/* returns true if the header contained in "s" is in list "t" */
int mutt_matches_ignore (const char *s, LIST *t)
{
  for (; t; t = t->next)
  {
    if (!ascii_strncasecmp (s, t->data, mutt_strlen (t->data)) || *t->data == '*')
      return 1;
  }
  return 0;
}

static void buffer_normalize_fullpath (BUFFER *dest, const char *src)
{
  enum { init, dot, dotdot, standard } state = init;

  mutt_buffer_clear (dest);
  if (!src)
    return;

  if (*src != '/')
  {
    mutt_buffer_strcpy (dest, src);
    return;
  }

  while (*src)
  {
    if (*src == '.')
    {
      switch (state)
      {
        case init:
          state = dot;
          break;
        case dot:
          state = dotdot;
          break;
        default:
          state = standard;
          break;
      }
    }
    else if (*src == '/')
    {
      switch (state)
      {
        case dot:
          dest->dptr -= 2;
          break;
        case dotdot:
          dest->dptr -= 3;
          if (dest->dptr != dest->data)
          {
            dest->dptr--;
            while (*dest->dptr != '/')
              dest->dptr--;
          }
          break;
        default:
          break;
      }
      state = init;
    }
    else
    {
      state = standard;
    }

    mutt_buffer_addch (dest, *src);
    src++;
  }

  /* Deal with a trailing /. or /.. */
  switch (state)
  {
    case dot:
      dest->dptr -= 2;
      if (dest->dptr == dest->data)
        dest->dptr++;
      *dest->dptr = '\0';
      break;
    case dotdot:
      dest->dptr -= 3;
      if (dest->dptr != dest->data)
      {
        dest->dptr--;
        while (*dest->dptr != '/')
          dest->dptr--;
      }
      if (dest->dptr == dest->data)
        dest->dptr++;
      *dest->dptr = '\0';
      break;
    default:
      break;
  }
}

/* Splits src into parts delimited by delimiter.
 * Invokes mapfunc on each part and joins the result back into src.
 * Note this function currently does not preserve trailing delimiters.
 */
static void delimited_buffer_map_join (BUFFER *src, const char *delimiter,
                                       void (*mapfunc)(BUFFER *))
{
  BUFFER *dest, *part;
  const char *part_begin, *part_end;
  size_t delim_size;

  delim_size = mutt_strlen (delimiter);
  if (!delim_size)
  {
    mapfunc (src);
    return;
  }

  dest = mutt_buffer_pool_get ();
  part = mutt_buffer_pool_get ();

  part_begin = mutt_b2s (src);
  while (part_begin && *part_begin)
  {
    part_end = strstr (part_begin, delimiter);
    if (part_end)
    {
      mutt_buffer_substrcpy (part, part_begin, part_end);
      part_end += delim_size;
    }
    else
      mutt_buffer_strcpy (part, part_begin);

    mapfunc (part);

    if (part_begin != mutt_b2s (src))
      mutt_buffer_addstr (dest, delimiter);
    mutt_buffer_addstr (dest, mutt_b2s (part));

    part_begin = part_end;
  }

  mutt_buffer_strcpy (src, mutt_b2s (dest));

  mutt_buffer_pool_release (&dest);
  mutt_buffer_pool_release (&part);
}

void mutt_buffer_expand_multi_path (BUFFER *src, const char *delimiter)
{
  delimited_buffer_map_join (src, delimiter, mutt_buffer_expand_path);
}

void mutt_buffer_expand_path (BUFFER *src)
{
  _mutt_buffer_expand_path (src, 0, 1);
}

/* Does expansion without relative path expansion */
void mutt_buffer_expand_path_norel (BUFFER *src)
{
  _mutt_buffer_expand_path (src, 0, 0);
}

void _mutt_buffer_expand_path (BUFFER *src, int rx, int expand_relative)
{
  BUFFER *p, *q, *tmp;
  const char *s, *tail = "";
  char *t;
  int recurse = 0;

  p = mutt_buffer_pool_get ();
  q = mutt_buffer_pool_get ();
  tmp = mutt_buffer_pool_get ();

  do
  {
    recurse = 0;
    s = mutt_b2s (src);

    switch (*s)
    {
      case '~':
      {
	if (*(s + 1) == '/' || *(s + 1) == 0)
	{
	  mutt_buffer_strcpy (p, NONULL(Homedir));
	  tail = s + 1;
	}
	else
	{
	  struct passwd *pw;
	  if ((t = strchr (s + 1, '/')))
	    *t = 0;

	  if ((pw = getpwnam (s + 1)))
	  {
	    mutt_buffer_strcpy (p, pw->pw_dir);
	    if (t)
	    {
	      *t = '/';
	      tail = t;
	    }
	    else
	      tail = "";
	  }
	  else
	  {
	    /* user not found! */
	    if (t)
	      *t = '/';
            mutt_buffer_clear (p);
	    tail = s;
	  }
	}
      }
      break;

      case '=':
      case '+':
      {
#ifdef USE_IMAP
	/* if folder = {host} or imap[s]://host/: don't append slash */
	if (mx_is_imap (NONULL (Maildir)) &&
	    (Maildir[strlen (Maildir) - 1] == '}' ||
	     Maildir[strlen (Maildir) - 1] == '/'))
	  mutt_buffer_strcpy (p, NONULL (Maildir));
	else
#endif
          if (Maildir && Maildir[strlen (Maildir) - 1] == '/')
            mutt_buffer_strcpy (p, NONULL (Maildir));
          else
            mutt_buffer_printf (p, "%s/", NONULL (Maildir));

	tail = s + 1;
      }
      break;

      /* elm compatibility, @ expands alias to user name */

      case '@':
      {
	HEADER *h;
	ADDRESS *alias;

	if ((alias = mutt_lookup_alias (s + 1)))
	{
	  h = mutt_new_header();
	  h->env = mutt_new_envelope();
	  h->env->from = h->env->to = alias;

          /* TODO: fix mutt_default_save() to use BUFFER */
          mutt_buffer_increase_size (p, _POSIX_PATH_MAX);
	  mutt_default_save (p->data, p->dsize, h);
          mutt_buffer_fix_dptr (p);

	  h->env->from = h->env->to = NULL;
	  mutt_free_header (&h);
	  /* Avoid infinite recursion if the resulting folder starts with '@' */
	  if (*(mutt_b2s (p)) != '@')
	    recurse = 1;

	  tail = "";
	}
      }
      break;

      case '>':
      {
	mutt_buffer_strcpy (p, NONULL(Inbox));
	tail = s + 1;
      }
      break;

      case '<':
      {
	mutt_buffer_strcpy (p, NONULL(Outbox));
	tail = s + 1;
      }
      break;

      case '!':
      {
	if (*(s+1) == '!')
	{
	  mutt_buffer_strcpy (p, NONULL(LastFolder));
	  tail = s + 2;
	}
	else
	{
	  mutt_buffer_strcpy (p, NONULL(Spoolfile));
	  tail = s + 1;
	}
      }
      break;

      case '-':
      {
	mutt_buffer_strcpy (p, NONULL(LastFolder));
	tail = s + 1;
      }
      break;

      case '^':
      {
	mutt_buffer_strcpy (p, NONULL(CurrentFolder));
	tail = s + 1;
      }
      break;

      default:
      {
	mutt_buffer_clear (p);
	tail = s;
      }
    }

    if (rx && *(mutt_b2s (p)) && !recurse)
    {
      mutt_rx_sanitize_string (q, mutt_b2s (p));
      mutt_buffer_printf (tmp, "%s%s", mutt_b2s (q), tail);
    }
    else
      mutt_buffer_printf (tmp, "%s%s", mutt_b2s (p), tail);

    mutt_buffer_strcpy (src, mutt_b2s (tmp));
  }
  while (recurse);

  mutt_buffer_pool_release (&p);
  mutt_buffer_pool_release (&q);

#ifdef USE_IMAP
  /* Rewrite IMAP path in canonical form - aids in string comparisons of
   * folders. May possibly fail, in which case s should be the same. */
  if (mx_is_imap (mutt_b2s (src)))
    imap_expand_path (src);
  else
#endif
    if (expand_relative &&
        (url_check_scheme (mutt_b2s (src)) == U_UNKNOWN) &&
        mutt_buffer_len (src) &&
        *mutt_b2s (src) != '/')
    {
      if (mutt_getcwd (tmp))
      {
        if (mutt_buffer_len (tmp) > 1)
          mutt_buffer_addch (tmp, '/');
        mutt_buffer_addstr (tmp, mutt_b2s (src));
        buffer_normalize_fullpath (src, mutt_b2s (tmp));
      }
    }

  mutt_buffer_pool_release (&tmp);
}

/* Extract the real name from /etc/passwd's GECOS field.
 * When set, honor the regular expression in GecosMask,
 * otherwise assume that the GECOS field is a
 * comma-separated list.
 * Replace "&" by a capitalized version of the user's login
 * name.
 */

char *mutt_gecos_name (char *dest, size_t destlen, struct passwd *pw)
{
  regmatch_t pat_match[1];
  size_t pwnl;
  int idx;
  char *p;

  if (!pw || !pw->pw_gecos)
    return NULL;

  memset (dest, 0, destlen);

  if (GecosMask.rx)
  {
    if (regexec (GecosMask.rx, pw->pw_gecos, 1, pat_match, 0) == 0)
      strfcpy (dest, pw->pw_gecos + pat_match[0].rm_so,
	       MIN (pat_match[0].rm_eo - pat_match[0].rm_so + 1, destlen));
  }
  else if ((p = strchr (pw->pw_gecos, ',')))
    strfcpy (dest, pw->pw_gecos, MIN (destlen, p - pw->pw_gecos + 1));
  else
    strfcpy (dest, pw->pw_gecos, destlen);

  pwnl = strlen (pw->pw_name);

  for (idx = 0; dest[idx]; idx++)
  {
    if (dest[idx] == '&')
    {
      memmove (&dest[idx + pwnl], &dest[idx + 1],
	       MAX((ssize_t)(destlen - idx - pwnl - 1), 0));
      memcpy (&dest[idx], pw->pw_name, MIN(destlen - idx - 1, pwnl));
      dest[idx] = toupper ((unsigned char) dest[idx]);
    }
  }

  return dest;
}


char *mutt_get_parameter (const char *s, PARAMETER *p)
{
  for (; p; p = p->next)
    if (ascii_strcasecmp (s, p->attribute) == 0)
      return (p->value);

  return NULL;
}

void mutt_set_parameter (const char *attribute, const char *value, PARAMETER **p)
{
  PARAMETER *q;

  if (!value)
  {
    mutt_delete_parameter (attribute, p);
    return;
  }

  for (q = *p; q; q = q->next)
  {
    if (ascii_strcasecmp (attribute, q->attribute) == 0)
    {
      mutt_str_replace (&q->value, value);
      return;
    }
  }

  q = mutt_new_parameter();
  q->attribute = safe_strdup(attribute);
  q->value = safe_strdup(value);
  q->next = *p;
  *p = q;
}

void mutt_delete_parameter (const char *attribute, PARAMETER **p)
{
  PARAMETER *q;

  for (q = *p; q; p = &q->next, q = q->next)
  {
    if (ascii_strcasecmp (attribute, q->attribute) == 0)
    {
      *p = q->next;
      q->next = NULL;
      mutt_free_parameter (&q);
      return;
    }
  }
}

/* returns 1 if Mutt can't display this type of data, 0 otherwise */
int mutt_needs_mailcap (BODY *m)
{
  switch (m->type)
  {
    case TYPETEXT:
      /* we can display any text, overridable by auto_view */
      return 0;
      break;

    case TYPEAPPLICATION:
      if ((WithCrypto & APPLICATION_PGP) && mutt_is_application_pgp(m))
	return 0;
      if ((WithCrypto & APPLICATION_SMIME) && mutt_is_application_smime(m))
	return 0;
      break;

    case TYPEMULTIPART:
    case TYPEMESSAGE:
      return 0;
  }

  return 1;
}

int mutt_is_text_part (BODY *b)
{
  int t = b->type;
  char *s = b->subtype;

  if ((WithCrypto & APPLICATION_PGP) && mutt_is_application_pgp (b))
    return 0;

  if (t == TYPETEXT)
    return 1;

  if (t == TYPEMESSAGE)
  {
    if (!ascii_strcasecmp ("delivery-status", s))
      return 1;
  }

  if ((WithCrypto & APPLICATION_PGP) && t == TYPEAPPLICATION)
  {
    if (!ascii_strcasecmp ("pgp-keys", s))
      return 1;
  }

  return 0;
}

#ifdef USE_AUTOCRYPT
void mutt_free_autocrypthdr (AUTOCRYPTHDR **p)
{
  AUTOCRYPTHDR *cur;

  if (!p)
    return;

  while (*p)
  {
    cur = *p;
    *p = (*p)->next;
    FREE (&cur->addr);
    FREE (&cur->keydata);
    FREE (&cur);
  }
}
#endif

void mutt_free_envelope (ENVELOPE **p)
{
  if (!*p) return;
  rfc822_free_address (&(*p)->return_path);
  rfc822_free_address (&(*p)->from);
  rfc822_free_address (&(*p)->to);
  rfc822_free_address (&(*p)->cc);
  rfc822_free_address (&(*p)->bcc);
  rfc822_free_address (&(*p)->sender);
  rfc822_free_address (&(*p)->reply_to);
  rfc822_free_address (&(*p)->mail_followup_to);

  FREE (&(*p)->list_post);
  FREE (&(*p)->subject);
  /* real_subj is just an offset to subject and shouldn't be freed */
  FREE (&(*p)->disp_subj);
  FREE (&(*p)->message_id);
  FREE (&(*p)->supersedes);
  FREE (&(*p)->date);
  FREE (&(*p)->x_label);

  mutt_buffer_free (&(*p)->spam);

  mutt_free_list (&(*p)->references);
  mutt_free_list (&(*p)->in_reply_to);
  mutt_free_list (&(*p)->userhdrs);

#ifdef USE_AUTOCRYPT
  mutt_free_autocrypthdr (&(*p)->autocrypt);
  mutt_free_autocrypthdr (&(*p)->autocrypt_gossip);
#endif

  FREE (p);		/* __FREE_CHECKED__ */
}

/* move all the headers from extra not present in base into base */
void mutt_merge_envelopes(ENVELOPE* base, ENVELOPE** extra)
{
  /* copies each existing element if necessary, and sets the element
   * to NULL in the source so that mutt_free_envelope doesn't leave us
   * with dangling pointers. */
#define MOVE_ELEM(h) if (!base->h) { base->h = (*extra)->h; (*extra)->h = NULL; }
  MOVE_ELEM(return_path);
  MOVE_ELEM(from);
  MOVE_ELEM(to);
  MOVE_ELEM(cc);
  MOVE_ELEM(bcc);
  MOVE_ELEM(sender);
  MOVE_ELEM(reply_to);
  MOVE_ELEM(mail_followup_to);
  MOVE_ELEM(list_post);
  MOVE_ELEM(message_id);
  MOVE_ELEM(supersedes);
  MOVE_ELEM(date);
  if (!(base->changed & MUTT_ENV_CHANGED_XLABEL))
  {
    MOVE_ELEM(x_label);
  }
  if (!(base->changed & MUTT_ENV_CHANGED_REFS))
  {
    MOVE_ELEM(references);
  }
  if (!(base->changed & MUTT_ENV_CHANGED_IRT))
  {
    MOVE_ELEM(in_reply_to);
  }

  /* real_subj is subordinate to subject */
  if (!base->subject)
  {
    base->subject = (*extra)->subject;
    base->real_subj = (*extra)->real_subj;
    base->disp_subj = (*extra)->disp_subj;
    (*extra)->subject = NULL;
    (*extra)->real_subj = NULL;
    (*extra)->disp_subj = NULL;
  }
  /* spam and user headers should never be hashed, and the new envelope may
   * have better values. Use new versions regardless. */
  mutt_buffer_free (&base->spam);
  mutt_free_list (&base->userhdrs);
  MOVE_ELEM(spam);
  MOVE_ELEM(userhdrs);
#undef MOVE_ELEM

  mutt_free_envelope(extra);
}

void _mutt_buffer_mktemp (BUFFER *buf, const char *prefix, const char *suffix,
                          const char *src, int line)
{
  RANDOM64 random64;
  mutt_random_bytes((char *)random64.char_array, sizeof(random64));

  mutt_buffer_printf (buf, "%s/%s-%s-%d-%d-%"PRIu64"%s%s",
                      NONULL (Tempdir), NONULL (prefix), NONULL (Hostname),
                      (int) getuid (), (int) getpid (), random64.int_64,
                      suffix ? "." : "", NONULL (suffix));
  dprint (3, (debugfile, "%s:%d: mutt_mktemp returns \"%s\".\n", src, line, mutt_b2s (buf)));
  if (unlink (mutt_b2s (buf)) && errno != ENOENT)
    dprint (1, (debugfile, "%s:%d: ERROR: unlink(\"%s\"): %s (errno %d)\n",
                src, line, mutt_b2s (buf), strerror (errno), errno));
}

void _mutt_mktemp (char *s, size_t slen, const char *prefix, const char *suffix,
                   const char *src, int line)
{
  RANDOM64 random64;
  mutt_random_bytes((char *) random64.char_array, sizeof(random64));

  size_t n = snprintf (s, slen, "%s/%s-%s-%d-%d-%"PRIu64"%s%s",
                       NONULL (Tempdir), NONULL (prefix), NONULL (Hostname),
                       (int) getuid (), (int) getpid (), random64.int_64,
                       suffix ? "." : "", NONULL (suffix));
  if (n >= slen)
    dprint (1, (debugfile, "%s:%d: ERROR: insufficient buffer space to hold temporary filename! slen=%zu but need %zu\n",
                src, line, slen, n));
  dprint (3, (debugfile, "%s:%d: mutt_mktemp returns \"%s\".\n", src, line, s));
  if (unlink (s) && errno != ENOENT)
    dprint (1, (debugfile, "%s:%d: ERROR: unlink(\"%s\"): %s (errno %d)\n", src, line, s, strerror (errno), errno));
}

/* these characters must be escaped in regular expressions */

static const char rx_special_chars[] = "^.[$()|*+?{\\";

int mutt_rx_sanitize_string (BUFFER *dest, const char *src)
{
  mutt_buffer_clear (dest);
  while (*src)
  {
    if (strchr (rx_special_chars, *src))
      mutt_buffer_addch (dest, '\\');
    mutt_buffer_addch (dest, *src++);
  }
  return 0;
}

void mutt_free_alias (ALIAS **p)
{
  ALIAS *t;

  while (*p)
  {
    t = *p;
    *p = (*p)->next;
    mutt_alias_delete_reverse (t);
    FREE (&t->name);
    rfc822_free_address (&t->addr);
    FREE (&t);
  }
}

void mutt_buffer_pretty_multi_mailbox (BUFFER *s, const char *delimiter)
{
  delimited_buffer_map_join (s, delimiter, mutt_buffer_pretty_mailbox);
}

void mutt_buffer_pretty_mailbox (BUFFER *s)
{
  /* This reduces the size of the BUFFER, so we can pass it through.
   * We adjust the size just to make sure s->data is not NULL though */
  mutt_buffer_increase_size (s, _POSIX_PATH_MAX);
  mutt_pretty_mailbox (s->data, s->dsize);
  mutt_buffer_fix_dptr (s);
}

/* collapse the pathname using ~ or = when possible */
void mutt_pretty_mailbox (char *s, size_t buflen)
{
  char *p = s, *q = s;
  size_t len;
  url_scheme_t scheme;
  char tmp[PATH_MAX];

  scheme = url_check_scheme (s);

#ifdef USE_IMAP
  if (scheme == U_IMAP || scheme == U_IMAPS)
  {
    imap_pretty_mailbox (s, buflen);
    return;
  }
#endif

  /* if s is an url, only collapse path component */
  if (scheme != U_UNKNOWN)
  {
    p = strchr(s, ':')+1;
    if (!strncmp (p, "//", 2))
      q = strchr (p+2, '/');
    if (!q)
      q = strchr (p, '\0');
    p = q;
  }

  /* cleanup path */
  if (strstr (p, "//") || strstr (p, "/./"))
  {
    /* first attempt to collapse the pathname, this is more
     * lightweight than realpath() and doesn't resolve links
     */
    while (*p)
    {
      if (*p == '/' && p[1] == '/')
      {
	*q++ = '/';
	p += 2;
      }
      else if (p[0] == '/' && p[1] == '.' && p[2] == '/')
      {
	*q++ = '/';
	p += 3;
      }
      else
	*q++ = *p++;
    }
    *q = 0;
  }
  else if (strstr (p, "..") &&
	   (scheme == U_UNKNOWN || scheme == U_FILE) &&
	   realpath (p, tmp))
    strfcpy (p, tmp, buflen - (p - s));

  if (mutt_strncmp (s, Maildir, (len = mutt_strlen (Maildir))) == 0 &&
      s[len] == '/')
  {
    *s++ = '=';
    memmove (s, s + len, mutt_strlen (s + len) + 1);
  }
  else
  {
    BUFFER *cwd = mutt_buffer_pool_get ();
    BUFFER *home = mutt_buffer_pool_get ();
    int under_cwd = 0, under_home = 0, cwd_under_home = 0;
    size_t cwd_len, home_len;

    mutt_getcwd (cwd);
    if (mutt_buffer_len (cwd) > 1)
      mutt_buffer_addch (cwd, '/');
    cwd_len = mutt_buffer_len (cwd);

    mutt_buffer_strcpy (home, NONULL (Homedir));
    if (mutt_buffer_len (home) > 1)
      mutt_buffer_addch (home, '/');
    home_len = mutt_buffer_len (home);

    if (cwd_len &&
        mutt_strncmp (s, mutt_b2s (cwd), cwd_len) == 0)
      under_cwd = 1;

    if (home_len &&
        mutt_strncmp (s, mutt_b2s (home), home_len) == 0)
      under_home = 1;

    if (under_cwd && under_home && (home_len < cwd_len))
      cwd_under_home = 1;

    if (under_cwd && (!under_home || cwd_under_home))
      memmove (s, s + cwd_len, mutt_strlen (s + cwd_len) + 1);
    else if (under_home && (home_len > 1))
    {
      *s++ = '~';
      memmove (s, s + home_len - 2, mutt_strlen (s + home_len - 2) + 1);
    }

    mutt_buffer_pool_release (&cwd);
    mutt_buffer_pool_release (&home);
  }
}

void mutt_pretty_size (char *s, size_t len, LOFF_T n)
{
  if (option (OPTSIZESHOWBYTES) && (n < 1024))
    snprintf (s, len, "%d", (int)n);
  else if (n == 0)
    strfcpy (s,
             option (OPTSIZEUNITSONLEFT) ? "K0" : "0K",
             len);
  else if (option (OPTSIZESHOWFRACTIONS) && (n < 10189)) /* 0.1K - 9.9K */
  {
    snprintf (s, len,
              option (OPTSIZEUNITSONLEFT) ? "K%3.1f" : "%3.1fK",
              (n < 103) ? 0.1 : n / 1024.0);
  }
  else if (!option (OPTSIZESHOWMB) || (n < 1023949)) /* 10K - 999K */
  {
    /* 51 is magic which causes 10189/10240 to be rounded up to 10 */
    snprintf (s, len,
              option (OPTSIZEUNITSONLEFT) ? ("K" OFF_T_FMT) : (OFF_T_FMT "K"),
              (n + 51) / 1024);
  }
  else if (option (OPTSIZESHOWFRACTIONS) && (n < 10433332)) /* 1.0M - 9.9M */
  {
    snprintf (s, len,
              option (OPTSIZEUNITSONLEFT) ? "M%3.1f" : "%3.1fM",
              n / 1048576.0);
  }
  else /* 10M+ */
  {
    /* (10433332 + 52428) / 1048576 = 10 */
    snprintf (s, len,
              option (OPTSIZEUNITSONLEFT) ?  ("M" OFF_T_FMT) : (OFF_T_FMT "M"),
              (n + 52428) / 1048576);
  }
}

void _mutt_buffer_quote_filename (BUFFER *d, const char *f, int add_outer)
{
  mutt_buffer_clear (d);

  if (!f)
    return;

  if (add_outer)
    mutt_buffer_addch (d, '\'');

  for (; *f; f++)
  {
    if (*f == '\'' || *f == '`')
    {
      mutt_buffer_addch (d, '\'');
      mutt_buffer_addch (d, '\\');
      mutt_buffer_addch (d, *f);
      mutt_buffer_addch (d, '\'');
    }
    else
      mutt_buffer_addch (d, *f);
  }

  if (add_outer)
    mutt_buffer_addch (d, '\'');
}

static const char safe_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+@{}._-:%/";

void mutt_buffer_sanitize_filename (BUFFER *d, const char *f, short slash)
{
  mutt_buffer_clear (d);

  if (!f)
    return;

  for (; *f; f++)
  {
    if ((slash && *f == '/') || !strchr (safe_chars, *f))
      mutt_buffer_addch (d, '_');
    else
      mutt_buffer_addch (d, *f);
  }
}

void mutt_expand_file_fmt (BUFFER *dest, const char *fmt, const char *src)
{
  BUFFER *tmp;

  tmp = mutt_buffer_pool_get ();
  mutt_buffer_quote_filename (tmp, src);
  mutt_expand_fmt (dest, fmt, mutt_b2s (tmp));
  mutt_buffer_pool_release (&tmp);
}

void mutt_expand_fmt (BUFFER *dest, const char *fmt, const char *src)
{
  const char *p;
  int found = 0;

  mutt_buffer_clear (dest);

  for (p = fmt; *p; p++)
  {
    if (*p == '%')
    {
      switch (p[1])
      {
	case '%':
          mutt_buffer_addch (dest, *p++);
	  break;
	case 's':
	  found = 1;
	  mutt_buffer_addstr (dest, src);
	  p++;
	  break;
	default:
	  mutt_buffer_addch (dest, *p);
	  break;
      }
    }
    else
    {
      mutt_buffer_addch (dest, *p);
    }
  }

  if (!found)
  {
    mutt_buffer_addch (dest, ' ');
    mutt_buffer_addstr (dest, src);
  }
}

/* return 0 on success, -1 on abort, 1 on error */
int mutt_check_overwrite (const char *attname, const char *path,
                          BUFFER *fname, int *append, char **directory)
{
  int rc = 0;
  BUFFER *tmp = NULL;
  struct stat st;

  mutt_buffer_strcpy (fname, path);
  if (access (mutt_b2s (fname), F_OK) != 0)
    return 0;
  if (stat (mutt_b2s (fname), &st) != 0)
    return -1;
  if (S_ISDIR (st.st_mode))
  {
    if (directory)
    {
      switch (mutt_multi_choice
              /* L10N:
                 Means "The path you specified as the destination file is a directory."
                 See the msgid "Save to file: " (alias.c, recvattach.c) */
	      (_("File is a directory, save under it? [(y)es, (n)o, (a)ll]"), _("yna")))
      {
	case 3:		/* all */
	  mutt_str_replace (directory, mutt_b2s (fname));
	  break;
	case 1:		/* yes */
	  FREE (directory);		/* __FREE_CHECKED__ */
	  break;
	case -1:	/* abort */
	  FREE (directory); 		/* __FREE_CHECKED__ */
	  return -1;
	case  2:	/* no */
	  FREE (directory);		/* __FREE_CHECKED__ */
	  return 1;
      }
    }
    /* L10N:
       Means "The path you specified as the destination file is a directory."
       See the msgid "Save to file: " (alias.c, recvattach.c) */
    else if ((rc = mutt_yesorno (_("File is a directory, save under it?"), MUTT_YES)) != MUTT_YES)
      return (rc == MUTT_NO) ? 1 : -1;

    tmp = mutt_buffer_pool_get ();
    mutt_buffer_strcpy (tmp, mutt_basename (NONULL (attname)));
    if ((mutt_buffer_get_field (_("File under directory: "), tmp,
                                MUTT_FILE | MUTT_CLEAR) != 0) ||
        !mutt_buffer_len (tmp))
    {
      mutt_buffer_pool_release (&tmp);
      return (-1);
    }
    mutt_buffer_concat_path (fname, path, mutt_b2s (tmp));
    mutt_buffer_pool_release (&tmp);
  }

  if (*append == 0 && access (mutt_b2s (fname), F_OK) == 0)
  {
    switch (mutt_multi_choice
	    (_("File exists, (o)verwrite, (a)ppend, or (c)ancel?"), _("oac")))
    {
      case -1: /* abort */
        return -1;
      case 3:  /* cancel */
	return 1;

      case 2: /* append */
        *append = MUTT_SAVE_APPEND;
        break;
      case 1: /* overwrite */
        *append = MUTT_SAVE_OVERWRITE;
        break;
    }
  }
  return 0;
}

void mutt_save_path (char *d, size_t dsize, ADDRESS *a)
{
  if (a && a->mailbox)
  {
    strfcpy (d, a->mailbox, dsize);
    if (!option (OPTSAVEADDRESS))
    {
      char *p;

      if ((p = strpbrk (d, "%@")))
	*p = 0;
    }
    mutt_strlower (d);
  }
  else
    *d = 0;
}

void mutt_buffer_save_path (BUFFER *dest, ADDRESS *a)
{
  if (a && a->mailbox)
  {
    mutt_buffer_strcpy (dest, a->mailbox);
    if (!option (OPTSAVEADDRESS))
    {
      char *p;

      if ((p = strpbrk (dest->data, "%@")))
      {
	*p = 0;
        mutt_buffer_fix_dptr (dest);
      }
    }
    mutt_strlower (dest->data);
  }
  else
    mutt_buffer_clear (dest);
}

void mutt_safe_path (BUFFER *dest, ADDRESS *a)
{
  char *p;

  mutt_buffer_save_path (dest, a);
  for (p = dest->data; *p; p++)
    if (*p == '/' || ISSPACE (*p) || !IsPrint ((unsigned char) *p))
      *p = '_';
}

void mutt_buffer_concat_path (BUFFER *d, const char *dir, const char *fname)
{
  const char *fmt = "%s/%s";

  if (!*fname || (*dir && dir[strlen(dir)-1] == '/'))
    fmt = "%s%s";

  mutt_buffer_printf (d, fmt, dir, fname);
}

/*
 * Write the concatened pathname (dir + "/" + fname) into dst.
 * The slash is omitted when dir or fname is of 0 length.
 */
void mutt_buffer_concatn_path (BUFFER *dst, const char *dir, size_t dirlen,
                               const char *fname, size_t fnamelen)
{
  mutt_buffer_clear (dst);
  if (dirlen)
    mutt_buffer_addstr_n (dst, dir, dirlen);
  if (dirlen && fnamelen)
    mutt_buffer_addch (dst, '/');
  if (fnamelen)
    mutt_buffer_addstr_n (dst, fname, fnamelen);
}

const char *mutt_getcwd (BUFFER *cwd)
{
  char *retval;

  mutt_buffer_increase_size (cwd, _POSIX_PATH_MAX);
  retval = getcwd (cwd->data, cwd->dsize);
  while (!retval && errno == ERANGE)
  {
    mutt_buffer_increase_size (cwd, cwd->dsize + STRING);
    retval = getcwd (cwd->data, cwd->dsize);
  }
  if (retval)
    mutt_buffer_fix_dptr (cwd);
  else
    mutt_buffer_clear (cwd);

  return retval;
}

/* Note this function uses a fixed size buffer of LONG_STRING and so
 * should only be used for visual modifications, such as disp_subj. */
char *mutt_apply_replace (char *dbuf, size_t dlen, char *sbuf, REPLACE_LIST *rlist)
{
  REPLACE_LIST *l;
  static regmatch_t *pmatch = NULL;
  static int nmatch = 0;
  static char twinbuf[2][LONG_STRING];
  int switcher = 0;
  char *p;
  int i, n;
  size_t cpysize, tlen;
  char *src, *dst;

  if (dbuf && dlen)
    dbuf[0] = '\0';

  if (sbuf == NULL || *sbuf == '\0' || (dbuf && !dlen))
    return dbuf;

  twinbuf[0][0] = '\0';
  twinbuf[1][0] = '\0';
  src = twinbuf[switcher];
  dst = src;

  strfcpy(src, sbuf, LONG_STRING);

  for (l = rlist; l; l = l->next)
  {
    /* If this pattern needs more matches, expand pmatch. */
    if (l->nmatch > nmatch)
    {
      safe_realloc (&pmatch, l->nmatch * sizeof(regmatch_t));
      nmatch = l->nmatch;
    }

    if (regexec (l->rx->rx, src, l->nmatch, pmatch, 0) == 0)
    {
      tlen = 0;
      switcher ^= 1;
      dst = twinbuf[switcher];

      dprint (5, (debugfile, "mutt_apply_replace: %s matches %s\n", src, l->rx->pattern));

      /* Copy into other twinbuf with substitutions */
      if (l->template)
      {
        for (p = l->template; *p && (tlen < LONG_STRING - 1); )
        {
	  if (*p == '%')
	  {
	    p++;
	    if (*p == 'L')
	    {
	      p++;
              cpysize = MIN (pmatch[0].rm_so, LONG_STRING - tlen - 1);
	      strncpy(&dst[tlen], src, cpysize);
	      tlen += cpysize;
	    }
	    else if (*p == 'R')
	    {
	      p++;
              cpysize = MIN (strlen (src) - pmatch[0].rm_eo, LONG_STRING - tlen - 1);
	      strncpy(&dst[tlen], &src[pmatch[0].rm_eo], cpysize);
	      tlen += cpysize;
	    }
	    else
	    {
	      n = strtoul(p, &p, 10);               /* get subst number */
	      while (isdigit((unsigned char)*p))    /* skip subst token */
                ++p;
	      for (i = pmatch[n].rm_so; (i < pmatch[n].rm_eo) && (tlen < LONG_STRING-1); i++)
	        dst[tlen++] = src[i];
	    }
	  }
	  else
	    dst[tlen++] = *p++;
        }
      }
      dst[tlen] = '\0';
      dprint (5, (debugfile, "mutt_apply_replace: subst %s\n", dst));
    }
    src = dst;
  }

  if (dbuf)
    strfcpy(dbuf, dst, dlen);
  else
    dbuf = safe_strdup(dst);
  return dbuf;
}


void mutt_FormatString (char *dest,		/* output buffer */
			size_t destlen,		/* output buffer len */
			size_t col,		/* starting column (nonzero when called recursively) */
                        int cols,               /* maximum columns */
			const char *src,	/* template string */
			format_t *callback,	/* callback for processing */
			void *data,		/* callback data */
			format_flag flags)	/* callback flags */
{
  char prefix[SHORT_STRING], buf[LONG_STRING], *cp, *wptr = dest, ch;
  char ifstring[SHORT_STRING], elsestring[SHORT_STRING];
  size_t wlen, count, len, wid;
  pid_t pid;
  FILE *filter;
  int n;
  char *recycler;

  prefix[0] = '\0';
  destlen--; /* save room for the terminal \0 */
  wlen = ((flags & MUTT_FORMAT_ARROWCURSOR) && option (OPTARROWCURSOR)) ? 3 : 0;
  col += wlen;

  if ((flags & MUTT_FORMAT_NOFILTER) == 0)
  {
    int off = -1;

    /* Do not consider filters if no pipe at end */
    n = mutt_strlen(src);
    if (n > 1 && src[n-1] == '|')
    {
      /* Scan backwards for backslashes */
      off = n;
      while (off > 0 && src[off-2] == '\\')
        off--;
    }

    /* If number of backslashes is even, the pipe is real. */
    /* n-off is the number of backslashes. */
    if (off > 0 && ((n-off) % 2) == 0)
    {
      BUFFER *srcbuf, *word, *command;
      char    srccopy[LONG_STRING];
#ifdef DEBUG
      int     i = 0;
#endif

      dprint(3, (debugfile, "fmtpipe = %s\n", src));

      strncpy(srccopy, src, n);
      srccopy[n-1] = '\0';

      /* prepare BUFFERs */
      srcbuf = mutt_buffer_from (srccopy);
      /* note: we are resetting dptr and *reading* from the buffer, so we don't
       * want to use mutt_buffer_clear(). */
      srcbuf->dptr = srcbuf->data;
      word = mutt_buffer_new ();
      command = mutt_buffer_new ();

      /* Iterate expansions across successive arguments */
      do
      {
        char *p;

        /* Extract the command name and copy to command line */
        dprint(3, (debugfile, "fmtpipe +++: %s\n", srcbuf->dptr));
        if (word->data)
          *word->data = '\0';
        mutt_extract_token(word, srcbuf, MUTT_TOKEN_NOLISP);
        dprint(3, (debugfile, "fmtpipe %2d: %s\n", i++, word->data));
        mutt_buffer_addch(command, '\'');
        mutt_FormatString(buf, sizeof(buf), 0, cols, word->data, callback, data,
                          flags | MUTT_FORMAT_NOFILTER);
        for (p = buf; p && *p; p++)
        {
          if (*p == '\'')
            /* shell quoting doesn't permit escaping a single quote within
             * single-quoted material.  double-quoting instead will lead
             * shell variable expansions, so break out of the single-quoted
             * span, insert a double-quoted single quote, and resume. */
            mutt_buffer_addstr(command, "'\"'\"'");
          else
            mutt_buffer_addch(command, *p);
        }
        mutt_buffer_addch(command, '\'');
        mutt_buffer_addch(command, ' ');
      } while (MoreArgs(srcbuf));

      dprint(3, (debugfile, "fmtpipe > %s\n", command->data));

      col -= wlen;	/* reset to passed in value */
      wptr = dest;      /* reset write ptr */
      wlen = ((flags & MUTT_FORMAT_ARROWCURSOR) && option (OPTARROWCURSOR)) ? 3 : 0;
      if ((pid = mutt_create_filter(command->data, NULL, &filter, NULL)) != -1)
      {
	int rc;

        n = fread(dest, 1, destlen /* already decremented */, filter);
        safe_fclose (&filter);
	rc = mutt_wait_filter(pid);
	if (rc != 0)
	  dprint(1, (debugfile, "format pipe command exited code %d\n", rc));
	if (n > 0)
        {
	  dest[n] = 0;
	  while ((n > 0) && (dest[n-1] == '\n' || dest[n-1] == '\r'))
	    dest[--n] = '\0';
	  dprint(3, (debugfile, "fmtpipe < %s\n", dest));

	  /* If the result ends with '%', this indicates that the filter
	   * generated %-tokens that mutt can expand.  Eliminate the '%'
	   * marker and recycle the string through mutt_FormatString().
	   * To literally end with "%", use "%%". */
	  if ((n > 0) && dest[n-1] == '%')
	  {
	    --n;
	    dest[n] = '\0';               /* remove '%' */
	    if ((n > 0) && dest[n-1] != '%')
	    {
	      recycler = safe_strdup(dest);
	      if (recycler)
	      {
		/* destlen is decremented at the start of this function
		 * to save space for the terminal nul char.  We can add
		 * it back for the recursive call since the expansion of
		 * format pipes does not try to append a nul itself.
		 */
		mutt_FormatString(dest, destlen+1, col, cols, recycler, callback, data, flags);
		FREE(&recycler);
	      }
	    }
	  }
	}
	else
	{
	  /* read error */
	  dprint(1, (debugfile, "error reading from fmtpipe: %s (errno=%d)\n", strerror(errno), errno));
	  *wptr = 0;
	}
      }
      else
      {
        /* Filter failed; erase write buffer */
        *wptr = '\0';
      }

      mutt_buffer_free(&command);
      mutt_buffer_free(&srcbuf);
      mutt_buffer_free(&word);
      return;
    }
  }

  while (*src && wlen < destlen)
  {
    if (*src == '%')
    {
      if (*++src == '%')
      {
	*wptr++ = '%';
	wlen++;
	col++;
	src++;
	continue;
      }

      if (*src == '?')
      {
	flags |= MUTT_FORMAT_OPTIONAL;
	src++;
      }
      else
      {
	flags &= ~MUTT_FORMAT_OPTIONAL;

	/* eat the format string */
	cp = prefix;
	count = 0;
	while (count < sizeof (prefix) &&
	       (isdigit ((unsigned char) *src) || *src == '.' || *src == '-' || *src == '='))
	{
	  *cp++ = *src++;
	  count++;
	}
	*cp = 0;
      }

      if (!*src)
	break; /* bad format */

      ch = *src++; /* save the character to switch on */

      if (flags & MUTT_FORMAT_OPTIONAL)
      {
        if (*src != '?')
          break; /* bad format */
        src++;

        /* eat the `if' part of the string */
        cp = ifstring;
	count = 0;
        while (count < sizeof (ifstring) && *src && *src != '?' && *src != '&')
	{
          *cp++ = *src++;
	  count++;
	}
        *cp = 0;

	/* eat the `else' part of the string (optional) */
	if (*src == '&')
	  src++; /* skip the & */
	cp = elsestring;
	count = 0;
	while (count < sizeof (elsestring) && *src && *src != '?')
	{
	  *cp++ = *src++;
	  count++;
	}
	*cp = 0;

	if (!*src)
	  break; /* bad format */

        src++; /* move past the trailing `?' */
      }

      /* handle generic cases first */
      if (ch == '>' || ch == '*')
      {
	/* %>X: right justify to EOL, left takes precedence
	 * %*X: right justify to EOL, right takes precedence */
	int soft = ch == '*';
	int pl, pw;
	if ((pl = mutt_charlen (src, &pw)) <= 0)
	  pl = pw = 1;

	/* see if there's room to add content, else ignore */
	if ((col < cols && wlen < destlen) || soft)
	{
	  int pad;

	  /* get contents after padding */
	  mutt_FormatString (buf, sizeof (buf), 0, cols, src + pl, callback, data, flags);
	  len = mutt_strlen (buf);
	  wid = mutt_strwidth (buf);

	  pad = (cols - col - wid) / pw;
	  if (pad >= 0)
	  {
            /* try to consume as many columns as we can, if we don't have
             * memory for that, use as much memory as possible */
            if (wlen + (pad * pl) + len > destlen)
              pad = (destlen > wlen + len) ? ((destlen - wlen - len) / pl) : 0;
            else
            {
              /* Add pre-spacing to make multi-column pad characters and
               * the contents after padding line up */
              while ((col + (pad * pw) + wid < cols) &&
                     (wlen + (pad * pl) + len < destlen))
              {
                *wptr++ = ' ';
                wlen++;
                col++;
              }
            }
	    while (pad-- > 0)
	    {
	      memcpy (wptr, src, pl);
	      wptr += pl;
	      wlen += pl;
	      col += pw;
	    }
	  }
	  else if (soft && pad < 0)
	  {
	    int offset = ((flags & MUTT_FORMAT_ARROWCURSOR) && option (OPTARROWCURSOR)) ? 3 : 0;
            int avail_cols = (cols > offset) ? (cols - offset) : 0;
	    /* \0-terminate dest for length computation in mutt_wstr_trunc() */
	    *wptr = 0;
	    /* make sure right part is at most as wide as display */
	    len = mutt_wstr_trunc (buf, destlen, avail_cols, &wid);
	    /* truncate left so that right part fits completely in */
	    wlen = mutt_wstr_trunc (dest, destlen - len, avail_cols - wid, &col);
	    wptr = dest + wlen;
            /* Multi-column characters may be truncated in the middle.
             * Add spacing so the right hand side lines up. */
            while ((col + wid < avail_cols) && (wlen + len < destlen))
            {
              *wptr++ = ' ';
              wlen++;
              col++;
            }
	  }
	  if (len + wlen > destlen)
	    len = mutt_wstr_trunc (buf, destlen - wlen, cols - col, NULL);
	  memcpy (wptr, buf, len);
	  wptr += len;
	  wlen += len;
	  col += wid;
	  src += pl;
	}
	break; /* skip rest of input */
      }
      else if (ch == '|')
      {
	/* pad to EOL */
	int pl, pw, c;
	if ((pl = mutt_charlen (src, &pw)) <= 0)
	  pl = pw = 1;

	/* see if there's room to add content, else ignore */
	if (col < cols && wlen < destlen)
	{
	  c = (cols - col) / pw;
	  if (c > 0 && wlen + (c * pl) > destlen)
	    c = ((signed)(destlen - wlen)) / pl;
	  while (c > 0)
	  {
	    memcpy (wptr, src, pl);
	    wptr += pl;
	    wlen += pl;
	    col += pw;
	    c--;
	  }
	  src += pl;
	}
	break; /* skip rest of input */
      }
      else
      {
	short tolower =  0;
	short nodots  = 0;

	while (ch == '_' || ch == ':')
	{
	  if (ch == '_')
	    tolower = 1;
	  else if (ch == ':')
	    nodots = 1;

	  ch = *src++;
	}

	/* use callback function to handle this case */
        *buf = '\0';
	src = callback (buf, sizeof (buf), col, cols, ch, src, prefix, ifstring, elsestring, data, flags);

	if (tolower)
	  mutt_strlower (buf);
	if (nodots)
	{
	  char *p = buf;
	  for (; *p; p++)
	    if (*p == '.')
              *p = '_';
	}

	if ((len = mutt_strlen (buf)) + wlen > destlen)
	  len = mutt_wstr_trunc (buf, destlen - wlen, cols - col, NULL);

	memcpy (wptr, buf, len);
	wptr += len;
	wlen += len;
	col += mutt_strwidth (buf);
      }
    }
    else if (*src == '\\')
    {
      if (!*++src)
	break;
      switch (*src)
      {
	case 'n':
	  *wptr = '\n';
	  break;
	case 't':
	  *wptr = '\t';
	  break;
	case 'r':
	  *wptr = '\r';
	  break;
	case 'f':
	  *wptr = '\f';
	  break;
	case 'v':
	  *wptr = '\v';
	  break;
	default:
	  *wptr = *src;
	  break;
      }
      src++;
      wptr++;
      wlen++;
      col++;
    }
    else
    {
      int tmp, w;
      /* in case of error, simply copy byte */
      if ((tmp = mutt_charlen (src, &w)) < 0)
	tmp = w = 1;
      if (tmp > 0 && wlen + tmp < destlen)
      {
        memcpy (wptr, src, tmp);
        wptr += tmp;
        src += tmp;
        wlen += tmp;
        col += w;
      }
      else
      {
	src += destlen - wlen;
	wlen = destlen;
      }
    }
  }
  *wptr = 0;
}

/* This function allows the user to specify a command to read stdout from in
   place of a normal file.  If the last character in the string is a pipe (|),
   then we assume it is a command to run instead of a normal file. */
FILE *mutt_open_read (const char *path, pid_t *thepid)
{
  FILE *f;
  struct stat s;

  int len = mutt_strlen (path);

  if (path[len - 1] == '|')
  {
    /* read from a pipe */

    char *s = safe_strdup (path);

    s[len - 1] = 0;
    mutt_endwin (NULL);
    *thepid = mutt_create_filter (s, NULL, &f, NULL);
    FREE (&s);
  }
  else
  {
    if (stat (path, &s) < 0)
      return (NULL);
    if (S_ISDIR (s.st_mode))
    {
      errno = EINVAL;
      return (NULL);
    }
    f = fopen (path, "r");
    *thepid = -1;
  }
  return (f);
}

/* returns 0 if OK to proceed, -1 to abort, 1 to retry */
int mutt_save_confirm (const char *s, struct stat *st)
{
  BUFFER *tmp = NULL;
  int ret = 0;
  int rc;
  int magic = 0;

  magic = mx_get_magic (s);

#ifdef USE_POP
  if (magic == MUTT_POP)
  {
    mutt_error _("Can't save message to POP mailbox.");
    return 1;
  }
#endif

  if (magic > 0 && !mx_access (s, W_OK))
  {
    if (option (OPTCONFIRMAPPEND))
    {
      tmp = mutt_buffer_pool_get ();
      mutt_buffer_printf (tmp, _("Append messages to %s?"), s);
      if ((rc = mutt_yesorno (mutt_b2s (tmp), MUTT_YES)) == MUTT_NO)
	ret = 1;
      else if (rc == -1)
	ret = -1;
      mutt_buffer_pool_release (&tmp);
    }
  }

  if (stat (s, st) != -1)
  {
    if (magic == -1)
    {
      mutt_error (_("%s is not a mailbox!"), s);
      return 1;
    }
  }
  else if (magic != MUTT_IMAP)
  {
    st->st_mtime = 0;
    st->st_atime = 0;

    if (errno == ENOENT)
    {
      if (option (OPTCONFIRMCREATE))
      {
        tmp = mutt_buffer_pool_get ();
	mutt_buffer_printf (tmp, _("Create %s?"), s);
	if ((rc = mutt_yesorno (mutt_b2s (tmp), MUTT_YES)) == MUTT_NO)
	  ret = 1;
	else if (rc == -1)
	  ret = -1;
        mutt_buffer_pool_release (&tmp);
      }
    }
    else
    {
      mutt_perror (s);
      return 1;
    }
  }

  mutt_window_clearline (MuttMessageWindow, 0);
  return (ret);
}

void state_prefix_putc (char c, STATE *s)
{
  if (s->flags & MUTT_PENDINGPREFIX)
  {
    state_reset_prefix (s);
    if (s->prefix)
      state_puts (s->prefix, s);
  }

  state_putc (c, s);

  if (c == '\n')
    state_set_prefix (s);
}

int state_printf (STATE *s, const char *fmt, ...)
{
  int rv;
  va_list ap;

  va_start (ap, fmt);
  rv = vfprintf (s->fpout, fmt, ap);
  va_end (ap);

  return rv;
}

void state_mark_attach (STATE *s)
{
  if ((s->flags & MUTT_DISPLAY) &&
      (!Pager || !mutt_strcmp (Pager, "builtin")))
    state_puts (AttachmentMarker, s);
}

void state_mark_protected_header (STATE *s)
{
  if ((s->flags & MUTT_DISPLAY) &&
      (!Pager || !mutt_strcmp (Pager, "builtin")))
    state_puts (ProtectedHeaderMarker, s);
}

void state_attach_puts (const char *t, STATE *s)
{
  if (*t != '\n') state_mark_attach (s);
  while (*t)
  {
    state_putc (*t, s);
    if (*t++ == '\n' && *t)
      if (*t != '\n') state_mark_attach (s);
  }
}

int state_putwc (wchar_t wc, STATE *s)
{
  char mb[MB_LEN_MAX] = "";
  int rc;

  if ((rc = wcrtomb (mb, wc, NULL)) < 0)
    return rc;
  if (fputs (mb, s->fpout) == EOF)
    return -1;
  return 0;
}

int state_putws (const wchar_t *ws, STATE *s)
{
  const wchar_t *p = ws;

  while (p && *p != L'\0')
  {
    if (state_putwc (*p, s) < 0)
      return -1;
    p++;
  }
  return 0;
}

void mutt_display_sanitize (char *s)
{
  for (; *s; s++)
  {
    if (!IsPrint (*s))
      *s = '?';
  }
}

void mutt_sleep (short s)
{
  if (SleepTime > s)
    sleep (SleepTime);
  else if (s)
    sleep(s);
}

/* Decrease a file's modification time by 1 second */

time_t mutt_decrease_mtime (const char *f, struct stat *st)
{
  struct utimbuf utim;
  struct stat _st;
  time_t mtime;
  int rc;

  if (!st)
  {
    if (stat (f, &_st) == -1)
      return -1;
    st = &_st;
  }

  if ((mtime = st->st_mtime) == time (NULL))
  {
    mtime -= 1;
    utim.actime = mtime;
    utim.modtime = mtime;
    do
      rc = utime (f, &utim);
    while (rc == -1 && errno == EINTR);

    if (rc == -1)
      return -1;
  }

  return mtime;
}

/* sets mtime of 'to' to mtime of 'from' */
void mutt_set_mtime (const char* from, const char* to)
{
  struct utimbuf utim;
  struct stat st;

  if (stat (from, &st) != -1)
  {
    utim.actime = st.st_mtime;
    utim.modtime = st.st_mtime;
    utime (to, &utim);
  }
}

/* set atime to current time, just as read() would do on !noatime.
 * Silently ignored if unsupported. */
void mutt_touch_atime (int f)
{
#ifdef HAVE_FUTIMENS
  struct timespec times[2]={{0,UTIME_NOW},{0,UTIME_OMIT}};
  futimens(f, times);
#endif
}

int mutt_timespec_compare (struct timespec *a, struct timespec *b)
{
  if (a->tv_sec < b->tv_sec)
    return -1;
  if (a->tv_sec > b->tv_sec)
    return 1;

  if (a->tv_nsec < b->tv_nsec)
    return -1;
  if (a->tv_nsec > b->tv_nsec)
    return 1;
  return 0;
}

void mutt_get_stat_timespec (struct timespec *dest, struct stat *sb, mutt_stat_type type)
{
  dest->tv_sec = 0;
  dest->tv_nsec = 0;

  switch (type)
  {
    case MUTT_STAT_ATIME:
      dest->tv_sec = sb->st_atime;
#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
      dest->tv_nsec = sb->st_atim.tv_nsec;
#endif
      break;
    case MUTT_STAT_MTIME:
      dest->tv_sec = sb->st_mtime;
#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
      dest->tv_nsec = sb->st_mtim.tv_nsec;
#endif
      break;
    case MUTT_STAT_CTIME:
      dest->tv_sec = sb->st_ctime;
#ifdef HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC
      dest->tv_nsec = sb->st_ctim.tv_nsec;
#endif
      break;
  }
}

int mutt_stat_timespec_compare (struct stat *sba, mutt_stat_type type, struct timespec *b)
{
  struct timespec a;

  mutt_get_stat_timespec (&a, sba, type);
  return mutt_timespec_compare (&a, b);
}

int mutt_stat_compare (struct stat *sba, mutt_stat_type sba_type,
                       struct stat *sbb, mutt_stat_type sbb_type)
{
  struct timespec a, b;

  mutt_get_stat_timespec (&a, sba, sba_type);
  mutt_get_stat_timespec (&b, sbb, sbb_type);
  return mutt_timespec_compare (&a, &b);
}

const char *mutt_make_version (void)
{
  static char vstring[STRING];
  snprintf (vstring, sizeof (vstring), "Mutt %s (%s)",
	    MUTT_VERSION, ReleaseDate);
  return vstring;
}

REGEXP *mutt_compile_regexp (const char *s, int flags)
{
  REGEXP *pp = safe_calloc (sizeof (REGEXP), 1);
  pp->pattern = safe_strdup (s);
  pp->rx = safe_calloc (sizeof (regex_t), 1);
  if (REGCOMP (pp->rx, NONULL(s), flags) != 0)
    mutt_free_regexp (&pp);

  return pp;
}

void mutt_free_regexp (REGEXP **pp)
{
  FREE (&(*pp)->pattern);
  regfree ((*pp)->rx);
  FREE (&(*pp)->rx);
  FREE (pp);		/* __FREE_CHECKED__ */
}

void mutt_free_rx_list (RX_LIST **list)
{
  RX_LIST *p;

  if (!list) return;
  while (*list)
  {
    p = *list;
    *list = (*list)->next;
    mutt_free_regexp (&p->rx);
    FREE (&p);
  }
}

void mutt_free_replace_list (REPLACE_LIST **list)
{
  REPLACE_LIST *p;

  if (!list) return;
  while (*list)
  {
    p = *list;
    *list = (*list)->next;
    mutt_free_regexp (&p->rx);
    FREE (&p->template);
    FREE (&p);
  }
}

int mutt_match_rx_list (const char *s, RX_LIST *l)
{
  if (!s)  return 0;

  for (; l; l = l->next)
  {
    if (regexec (l->rx->rx, s, (size_t) 0, (regmatch_t *) 0, (int) 0) == 0)
    {
      dprint (5, (debugfile, "mutt_match_rx_list: %s matches %s\n", s, l->rx->pattern));
      return 1;
    }
  }

  return 0;
}

/* Match a string against the patterns defined by the 'spam' command and output
 * the expanded format into `text` when there is a match.  If textsize<=0, the
 * match is performed but the format is not expanded and no assumptions are made
 * about the value of `text` so it may be NULL.
 *
 * Returns 1 if the argument `s` matches a pattern in the spam list, otherwise
 * 0. */
int mutt_match_spam_list (const char *s, REPLACE_LIST *l, char *text, int textsize)
{
  static regmatch_t *pmatch = NULL;
  static int nmatch = 0;
  int tlen = 0;
  char *p;

  if (!s) return 0;

  for (; l; l = l->next)
  {
    /* If this pattern needs more matches, expand pmatch. */
    if (l->nmatch > nmatch)
    {
      safe_realloc (&pmatch, l->nmatch * sizeof(regmatch_t));
      nmatch = l->nmatch;
    }

    /* Does this pattern match? */
    if (regexec (l->rx->rx, s, (size_t) l->nmatch, (regmatch_t *) pmatch, (int) 0) == 0)
    {
      dprint (5, (debugfile, "mutt_match_spam_list: %s matches %s\n", s, l->rx->pattern));
      dprint (5, (debugfile, "mutt_match_spam_list: %d subs\n", (int)l->rx->rx->re_nsub));

      /* Copy template into text, with substitutions. */
      for (p = l->template; *p && tlen < textsize - 1;)
      {
	/* backreference to pattern match substring, eg. %1, %2, etc) */
	if (*p == '%')
	{
	  char *e; /* used as pointer to end of integer backreference in strtol() call */
	  int n;

	  ++p; /* skip over % char */
	  n = strtol(p, &e, 10);
	  /* Ensure that the integer conversion succeeded (e!=p) and bounds check.  The upper bound check
	   * should not strictly be necessary since add_to_spam_list() finds the largest value, and
	   * the static array above is always large enough based on that value. */
	  if (e != p && n >= 0 && n <= l->nmatch && pmatch[n].rm_so != -1)
          {
	    /* copy as much of the substring match as will fit in the output buffer, saving space for
	     * the terminating nul char */
	    int idx;
	    for (idx = pmatch[n].rm_so; (idx < pmatch[n].rm_eo) && (tlen < textsize - 1); ++idx)
	      text[tlen++] = s[idx];
	  }
	  p = e; /* skip over the parsed integer */
	}
	else
	{
	  text[tlen++] = *p++;
	}
      }
      /* tlen should always be less than textsize except when textsize<=0
       * because the bounds checks in the above code leave room for the
       * terminal nul char.   This should avoid returning an unterminated
       * string to the caller.  When textsize<=0 we make no assumption about
       * the validity of the text pointer. */
      if (tlen < textsize)
      {
	text[tlen] = '\0';
	dprint (5, (debugfile, "mutt_match_spam_list: \"%s\"\n", text));
      }
      return 1;
    }
  }

  return 0;
}

void mutt_encode_path (BUFFER *dest, const char *src)
{
  char *p;
  int rc;

  p = safe_strdup (src);
  rc = mutt_convert_string (&p, Charset, "utf-8", 0);
  /* `src' may be NULL, such as when called from the pop3 driver. */
  mutt_buffer_strcpy (dest, (rc == 0) ? NONULL(p) : NONULL(src));
  FREE (&p);
}


/************************************************************************
 * These functions are transplanted from lib.c, in order to modify them *
 * to use BUFFERs.                                                      *
 ************************************************************************/

/* remove a directory and everything under it */
int mutt_rmtree (const char* path)
{
  DIR* dirp;
  struct dirent* de;
  BUFFER *cur = NULL;
  struct stat statbuf;
  int rc = 0;

  if (!(dirp = opendir (path)))
  {
    dprint (1, (debugfile, "mutt_rmtree: error opening directory %s\n", path));
    return -1;
  }

  /* We avoid using the buffer pool for this function, because it
   * invokes recursively to an unknown depth. */
  cur = mutt_buffer_new ();
  mutt_buffer_increase_size (cur, _POSIX_PATH_MAX);

  while ((de = readdir (dirp)))
  {
    if (!strcmp (".", de->d_name) || !strcmp ("..", de->d_name))
      continue;

    mutt_buffer_printf (cur, "%s/%s", path, de->d_name);
    /* XXX make nonrecursive version */

    if (stat(mutt_b2s (cur), &statbuf) == -1)
    {
      rc = 1;
      continue;
    }

    if (S_ISDIR (statbuf.st_mode))
      rc |= mutt_rmtree (mutt_b2s (cur));
    else
      rc |= unlink (mutt_b2s (cur));
  }
  closedir (dirp);

  rc |= rmdir (path);

  mutt_buffer_free (&cur);
  return rc;
}

/* Create a temporary directory next to a file name */

static int mutt_mkwrapdir (const char *path, BUFFER *newfile, BUFFER *newdir)
{
  const char *basename;
  BUFFER *parent = NULL;
  char *p;
  int rc = 0;

  parent = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (parent, NONULL (path));

  if ((p = strrchr (parent->data, '/')))
  {
    *p = '\0';
    basename = p + 1;
  }
  else
  {
    mutt_buffer_strcpy (parent, ".");
    basename = path;
  }

  mutt_buffer_printf (newdir, "%s/%s", mutt_b2s (parent), ".muttXXXXXX");
  if (mkdtemp(newdir->data) == NULL)
  {
    dprint(1, (debugfile, "mutt_mkwrapdir: mkdtemp() failed\n"));
    rc = -1;
    goto cleanup;
  }

  mutt_buffer_printf (newfile, "%s/%s", mutt_b2s (newdir), NONULL(basename));

cleanup:
  mutt_buffer_pool_release (&parent);
  return rc;
}

static int mutt_put_file_in_place (const char *path, const char *safe_file, const char *safe_dir)
{
  int rv;

  rv = safe_rename (safe_file, path);
  unlink (safe_file);
  rmdir (safe_dir);
  return rv;
}

int safe_open (const char *path, int flags)
{
  struct stat osb, nsb;
  int fd;
  BUFFER *safe_file = NULL;
  BUFFER *safe_dir = NULL;

  if (flags & O_EXCL)
  {
    safe_file = mutt_buffer_pool_get ();
    safe_dir = mutt_buffer_pool_get ();

    if (mutt_mkwrapdir (path, safe_file, safe_dir) == -1)
    {
      fd = -1;
      goto cleanup;
    }

    if ((fd = open (mutt_b2s (safe_file), flags, 0600)) < 0)
    {
      rmdir (mutt_b2s (safe_dir));
      goto cleanup;
    }

    /* NFS and I believe cygwin do not handle movement of open files well */
    close (fd);
    if (mutt_put_file_in_place (path, mutt_b2s (safe_file), mutt_b2s (safe_dir)) == -1)
    {
      fd = -1;
      goto cleanup;
    }
  }

  if ((fd = open (path, flags & ~O_EXCL, 0600)) < 0)
    goto cleanup;

  /* make sure the file is not symlink */
  if (lstat (path, &osb) < 0 || fstat (fd, &nsb) < 0 ||
      compare_stat(&osb, &nsb) == -1)
  {
/*    dprint (1, (debugfile, "safe_open(): %s is a symlink!\n", path)); */
    close (fd);
    fd = -1;
    goto cleanup;
  }

cleanup:
  mutt_buffer_pool_release (&safe_file);
  mutt_buffer_pool_release (&safe_dir);

  return (fd);
}

/* when opening files for writing, make sure the file doesn't already exist
 * to avoid race conditions.
 */
FILE *safe_fopen (const char *path, const char *mode)
{
  if (mode[0] == 'w')
  {
    int fd;
    int flags = O_CREAT | O_EXCL;

#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    if (mode[1] == '+')
      flags |= O_RDWR;
    else
      flags |= O_WRONLY;

    if ((fd = safe_open (path, flags)) < 0)
      return (NULL);

    return (fdopen (fd, mode));
  }
  else
    return (fopen (path, mode));
}

int safe_symlink(const char *oldpath, const char *newpath)
{
  struct stat osb, nsb;

  if (!oldpath || !newpath)
    return -1;

  if (unlink(newpath) == -1 && errno != ENOENT)
    return -1;

  if (oldpath[0] == '/')
  {
    if (symlink (oldpath, newpath) == -1)
      return -1;
  }
  else
  {
    BUFFER *abs_oldpath = NULL;

    abs_oldpath = mutt_buffer_pool_get ();

    if (mutt_getcwd (abs_oldpath) == NULL)
    {
      mutt_buffer_pool_release (&abs_oldpath);
      return -1;
    }

    mutt_buffer_addch (abs_oldpath, '/');
    mutt_buffer_addstr (abs_oldpath, oldpath);
    if (symlink (mutt_b2s (abs_oldpath), newpath) == -1)
    {
      mutt_buffer_pool_release (&abs_oldpath);
      return -1;
    }

    mutt_buffer_pool_release (&abs_oldpath);
  }

  if (stat(oldpath, &osb) == -1 || stat(newpath, &nsb) == -1
      || compare_stat(&osb, &nsb) == -1)
  {
    unlink(newpath);
    return -1;
  }

  return 0;
}

/* END lib.c transplant functions */
