/*
 * Copyright (C) 2001-2002 Oliver Ehli <elmy@acm.org>
 * Copyright (C) 2002 Mike Schiraldi <raldi@research.netsol.com>
 * Copyright (C) 2004 g10 Code GmbH
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
#include "mutt_curses.h"
#include "mutt_menu.h"
#include "smime.h"
#include "mime.h"
#include "copy.h"
#include "send.h"

#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#include <locale.h>

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif

#ifdef CRYPT_BACKEND_CLASSIC_SMIME

#include "mutt_crypt.h"

struct smime_command_context {
  const char *key;		    /* %k */
  const char *cryptalg;		    /* %a */
  const char *digestalg;	    /* %d */
  const char *fname;		    /* %f */
  const char *sig_fname;	    /* %s */
  const char *certificates;	    /* %c */
  const char *intermediates;        /* %i */
};


char SmimePass[STRING];
time_t SmimeExptime = 0; /* when does the cached passphrase expire? */


static BUFFER *SmimeKeyToUse = NULL;
static BUFFER *SmimeCertToUse = NULL;
static BUFFER *SmimeIntermediateToUse = NULL;

void smime_init (void)
{
  SmimeKeyToUse = mutt_buffer_new ();
  SmimeCertToUse = mutt_buffer_new ();
  SmimeIntermediateToUse = mutt_buffer_new ();
}

void smime_cleanup (void)
{
  mutt_buffer_free (&SmimeKeyToUse);
  mutt_buffer_free (&SmimeCertToUse);
  mutt_buffer_free (&SmimeIntermediateToUse);
}


void smime_free_key (smime_key_t **keylist)
{
  smime_key_t *key;

  if (!keylist)
    return;

  while (*keylist)
  {
    key = *keylist;
    *keylist = (*keylist)->next;

    FREE (&key->email);
    FREE (&key->hash);
    FREE (&key->label);
    FREE (&key->issuer);
    FREE (&key);
  }
}

static smime_key_t *smime_copy_key (smime_key_t *key)
{
  smime_key_t *copy;

  if (!key)
    return NULL;

  copy = safe_calloc (sizeof (smime_key_t), 1);
  copy->email  = safe_strdup(key->email);
  copy->hash   = safe_strdup(key->hash);
  copy->label  = safe_strdup(key->label);
  copy->issuer = safe_strdup(key->issuer);
  copy->trust  = key->trust;
  copy->flags  = key->flags;

  return copy;
}


/*
 *     Queries and passphrase handling.
 */




/* these are copies from pgp.c */


void smime_void_passphrase (void)
{
  memset (SmimePass, 0, sizeof (SmimePass));
  SmimeExptime = 0;
}

int smime_valid_passphrase (void)
{
  time_t now = time (NULL);

  if (now < SmimeExptime)
    /* Use cached copy.  */
    return 1;

  smime_void_passphrase();

  if (mutt_get_password (_("Enter S/MIME passphrase:"), SmimePass, sizeof (SmimePass)) == 0)
  {
    SmimeExptime = mutt_add_timeout (time (NULL), SmimeTimeout);
    return (1);
  }
  else
    SmimeExptime = 0;

  return 0;
}


/*
 *     The OpenSSL interface
 */

/* This is almost identical to ppgp's invoking interface. */

static const char *_mutt_fmt_smime_command (char *dest,
					    size_t destlen,
					    size_t col,
                                            int cols,
					    char op,
					    const char *src,
					    const char *prefix,
					    const char *ifstring,
					    const char *elsestring,
					    void *data,
					    format_flag flags)
{
  char fmt[16];
  struct smime_command_context *cctx = (struct smime_command_context *) data;
  int optional = (flags & MUTT_FORMAT_OPTIONAL);

  switch (op)
  {
    case 'C':
    {
      if (!optional)
      {
	BUFFER *path, *buf1, *buf2;
	struct stat sb;

        path = mutt_buffer_pool_get ();
        buf1 = mutt_buffer_pool_get ();
        buf2 = mutt_buffer_pool_get ();

	mutt_buffer_strcpy (path, NONULL (SmimeCALocation));
	mutt_buffer_expand_path (path);
	mutt_buffer_quote_filename (buf1, mutt_b2s (path));

	if (stat (mutt_b2s (path), &sb) != 0 ||
            !S_ISDIR (sb.st_mode))
	  mutt_buffer_printf (buf2, "-CAfile %s", mutt_b2s (buf1));
	else
	  mutt_buffer_printf (buf2, "-CApath %s", mutt_b2s (buf1));

	snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	snprintf (dest, destlen, fmt, mutt_b2s (buf2));

        mutt_buffer_pool_release (&path);
        mutt_buffer_pool_release (&buf1);
        mutt_buffer_pool_release (&buf2);
      }
      else if (!SmimeCALocation)
	optional = 0;
      break;
    }

    case 'c':
    {           /* certificate (list) */
      if (!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	snprintf (dest, destlen, fmt, NONULL(cctx->certificates));
      }
      else if (!cctx->certificates)
	optional = 0;
      break;
    }

    case 'i':
    {           /* intermediate certificates  */
      if (!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	snprintf (dest, destlen, fmt, NONULL(cctx->intermediates));
      }
      else if (!cctx->intermediates)
	optional = 0;
      break;
    }

    case 's':
    {           /* detached signature */
      if (!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	snprintf (dest, destlen, fmt, NONULL (cctx->sig_fname));
      }
      else if (!cctx->sig_fname)
	optional = 0;
      break;
    }

    case 'k':
    {           /* private key */
      if (!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	snprintf (dest, destlen, fmt, NONULL (cctx->key));
      }
      else if (!cctx->key)
	optional = 0;
      break;
    }

    case 'a':
    {           /* algorithm for encryption */
      if (!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	snprintf (dest, destlen, fmt, NONULL (cctx->cryptalg));
      }
      else if (!cctx->key)
	optional = 0;
      break;
    }

    case 'f':
    {           /* file to process */
      if (!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	snprintf (dest, destlen, fmt, NONULL (cctx->fname));
      }
      else if (!cctx->fname)
	optional = 0;
      break;
    }

    case 'd':
    {           /* algorithm for the signature message digest */
      if (!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	snprintf (dest, destlen, fmt, NONULL (cctx->digestalg));
      }
      else if (!cctx->key)
	optional = 0;
      break;
    }

    default:
      *dest = '\0';
      break;
  }

  if (optional)
    mutt_FormatString (dest, destlen, col, cols, ifstring, _mutt_fmt_smime_command,
		       data, 0);
  else if (flags & MUTT_FORMAT_OPTIONAL)
    mutt_FormatString (dest, destlen, col, cols, elsestring, _mutt_fmt_smime_command,
		       data, 0);

  return (src);
}



static void mutt_smime_command (char *d, size_t dlen,
				struct smime_command_context *cctx, const char *fmt)
{
  mutt_FormatString (d, dlen, 0, MuttIndexWindow->cols, NONULL(fmt), _mutt_fmt_smime_command,
                     cctx, 0);
  dprint (2,(debugfile, "mutt_smime_command: %s\n", d));
}




static pid_t smime_invoke (FILE **smimein, FILE **smimeout, FILE **smimeerr,
			   int smimeinfd, int smimeoutfd, int smimeerrfd,
			   const char *fname,
			   const char *sig_fname,
			   const char *cryptalg,
			   const char *digestalg,
			   const char *key,
			   const char *certificates,
			   const char *intermediates,
			   const char *format)
{
  struct smime_command_context cctx;
  char cmd[HUGE_STRING];

  memset (&cctx, 0, sizeof (cctx));

  if (!format || !*format)
    return (pid_t) -1;

  cctx.fname	       = fname;
  cctx.sig_fname       = sig_fname;
  cctx.key	       = key;
  cctx.cryptalg	       = cryptalg;
  cctx.digestalg       = digestalg;
  cctx.certificates    = certificates;
  cctx.intermediates   = intermediates;

  mutt_smime_command (cmd, sizeof (cmd), &cctx, format);

  return mutt_create_filter_fd (cmd, smimein, smimeout, smimeerr,
				smimeinfd, smimeoutfd, smimeerrfd);
}






/*
 *    Key and certificate handling.
 */


static char *smime_key_flags (int flags)
{
  static char buff[3];

  if (!(flags & KEYFLAG_CANENCRYPT))
    buff[0] = '-';
  else
    buff[0] = 'e';

  if (!(flags & KEYFLAG_CANSIGN))
    buff[1] = '-';
  else
    buff[1] = 's';

  buff[2] = '\0';

  return buff;
}


static void smime_entry (char *s, size_t l, MUTTMENU * menu, int num)
{
  smime_key_t **Table = (smime_key_t **) menu->data;
  smime_key_t *this = Table[num];
  char* truststate;
  switch (this->trust)
  {
    case 't':
      truststate = N_("Trusted   ");
      break;
    case 'v':
      truststate = N_("Verified  ");
      break;
    case 'u':
      truststate = N_("Unverified");
      break;
    case 'e':
      truststate = N_("Expired   ");
      break;
    case 'r':
      truststate = N_("Revoked   ");
      break;
    case 'i':
      truststate = N_("Invalid   ");
      break;
    default:
      truststate = N_("Unknown   ");
  }
  snprintf(s, l, " 0x%s %s %s %-35.35s %s", this->hash,
           smime_key_flags (this->flags), truststate, this->email, this->label);
}


static smime_key_t *smime_select_key (smime_key_t *keys, char *query)
{
  smime_key_t **table = NULL;
  int table_size = 0;
  int table_index = 0;
  smime_key_t *key = NULL;
  smime_key_t *selected_key = NULL;
  char helpstr[LONG_STRING];
  char buf[LONG_STRING];
  char title[256];
  MUTTMENU* menu;
  char *s = "";
  int done = 0;

  for (table_index = 0, key = keys; key; key = key->next)
  {
    if (table_index == table_size)
    {
      table_size += 5;
      safe_realloc (&table, sizeof (smime_key_t *) * table_size);
    }

    table[table_index++] = key;
  }

  snprintf(title, sizeof(title), _("S/MIME certificates matching \"%s\"."),
           query);

  /* Make Helpstring */
  helpstr[0] = 0;
  mutt_make_help (buf, sizeof (buf), _("Exit  "), MENU_SMIME, OP_EXIT);
  strcat (helpstr, buf);	/* __STRCAT_CHECKED__ */
  mutt_make_help (buf, sizeof (buf), _("Select  "), MENU_SMIME,
                  OP_GENERIC_SELECT_ENTRY);
  strcat (helpstr, buf);	/* __STRCAT_CHECKED__ */
  mutt_make_help (buf, sizeof(buf), _("Help"), MENU_SMIME, OP_HELP);
  strcat (helpstr, buf);	/* __STRCAT_CHECKED__ */

  /* Create the menu */
  menu = mutt_new_menu(MENU_SMIME);
  menu->max = table_index;
  menu->make_entry = smime_entry;
  menu->help = helpstr;
  menu->data = table;
  menu->title = title;
  mutt_push_current_menu (menu);
  /* sorting keys might be done later - TODO */

  mutt_clear_error();

  done = 0;
  while (!done)
  {
    switch (mutt_menuLoop (menu))
    {
      case OP_GENERIC_SELECT_ENTRY:
        if (table[menu->current]->trust != 't')
        {
          switch (table[menu->current]->trust)
          {
            case 'i':
            case 'r':
            case 'e':
              s = N_("ID is expired/disabled/revoked.");
              break;
            case 'u':
              s = N_("ID has undefined validity.");
              break;
            case 'v':
              s = N_("ID is not trusted.");
              break;
          }

          snprintf (buf, sizeof (buf), _("%s Do you really want to use the key?"),
                    _(s));

          if (mutt_yesorno (buf, MUTT_NO) != MUTT_YES)
          {
            mutt_clear_error ();
            break;
          }
        }

        selected_key = table[menu->current];
        done = 1;
        break;
      case OP_EXIT:
        done = 1;
        break;
    }
  }

  mutt_pop_current_menu (menu);
  mutt_menuDestroy (&menu);
  FREE (&table);

  return selected_key;
}

static smime_key_t *smime_parse_key(char *buf)
{
  smime_key_t *key;
  char *pend, *p;
  int field = 0;

  key = safe_calloc (sizeof (smime_key_t), 1);

  for (p = buf; p; p = pend)
  {
    /* Some users manually maintain their .index file, and use a tab
     * as a delimiter, which the old parsing code (using fscanf)
     * happened to allow.  smime_keys.pl uses a space, so search for both.
     */
    if ((pend = strchr (p, ' ')) || (pend = strchr (p, '\t')) ||
        (pend = strchr (p, '\n')))
      *pend++ = 0;

    /* For backward compatibility, don't count consecutive delimiters
     * as an empty field.
     */
    if (!*p)
      continue;

    field++;

    switch (field)
    {
      case 1:                   /* mailbox */
        key->email = safe_strdup (p);
        break;
      case 2:                   /* hash */
        key->hash = safe_strdup (p);
        break;
      case 3:                   /* label */
        key->label = safe_strdup (p);
        break;
      case 4:                   /* issuer */
        key->issuer = safe_strdup (p);
        break;
      case 5:                   /* trust */
        key->trust = *p;
        break;
      case 6:                   /* purpose */
        while (*p)
        {
          switch (*p++)
          {
            case 'e':
              key->flags |= KEYFLAG_CANENCRYPT;
              break;

            case 's':
              key->flags |= KEYFLAG_CANSIGN;
              break;
          }
        }
        break;
    }
  }

  /* Old index files could be missing issuer, trust, and purpose,
   * but anything less than that is an error. */
  if (field < 3)
  {
    smime_free_key (&key);
    return NULL;
  }

  if (field < 4)
    key->issuer = safe_strdup ("?");

  if (field < 5)
    key->trust = 't';

  if (field < 6)
    key->flags = (KEYFLAG_CANENCRYPT | KEYFLAG_CANSIGN);

  return key;
}

static smime_key_t *smime_get_candidates(char *search, short public)
{
  BUFFER *index_file;
  FILE *fp;
  char buf[LONG_STRING];
  smime_key_t *key, *results, **results_end;

  results = NULL;
  results_end = &results;

  index_file = mutt_buffer_pool_get ();
  mutt_buffer_printf (index_file, "%s/.index",
                      public ? NONULL(SmimeCertificates) : NONULL(SmimeKeys));

  if ((fp = safe_fopen (mutt_b2s (index_file), "r")) == NULL)
  {
    mutt_perror (mutt_b2s (index_file));
    mutt_buffer_pool_release (&index_file);
    return NULL;
  }
  mutt_buffer_pool_release (&index_file);

  while (fgets (buf, sizeof (buf), fp))
  {
    if ((! *search) || mutt_stristr (buf, search))
    {
      key = smime_parse_key (buf);
      if (key)
      {
        *results_end = key;
        results_end = &key->next;
      }
    }
  }

  safe_fclose (&fp);

  return results;
}

/* Returns the first matching key record, without prompting or checking of
 * abilities or trust.
 */
static smime_key_t *smime_get_key_by_hash(char *hash, short public)
{
  smime_key_t *results, *result;
  smime_key_t *match = NULL;

  results = smime_get_candidates(hash, public);
  for (result = results; result; result = result->next)
  {
    if (mutt_strcasecmp (hash, result->hash) == 0)
    {
      match = smime_copy_key (result);
      break;
    }
  }

  smime_free_key (&results);

  return match;
}

static smime_key_t *smime_get_key_by_addr(char *mailbox, short abilities, short public, short oppenc_mode)
{
  smime_key_t *results, *result;
  smime_key_t *matches = NULL;
  smime_key_t **matches_end = &matches;
  smime_key_t *match;
  smime_key_t *trusted_match = NULL;
  smime_key_t *valid_match = NULL;
  smime_key_t *return_key = NULL;
  int multi_trusted_matches = 0;

  if (! mailbox)
    return NULL;

  results = smime_get_candidates(mailbox, public);
  for (result = results; result; result = result->next)
  {
    if (abilities && !(result->flags & abilities))
    {
      continue;
    }

    if (mutt_strcasecmp (mailbox, result->email) == 0)
    {
      match = smime_copy_key (result);
      *matches_end = match;
      matches_end = &match->next;

      if (match->trust == 't')
      {
        if (trusted_match &&
            (mutt_strcasecmp (match->hash, trusted_match->hash) != 0))
        {
          multi_trusted_matches = 1;
        }
        trusted_match = match;
      }
      else if ((match->trust == 'u') || (match->trust == 'v'))
      {
        valid_match = match;
      }
    }
  }

  smime_free_key (&results);

  if (matches)
  {
    if (oppenc_mode)
    {
      if (trusted_match)
        return_key = smime_copy_key (trusted_match);
      else if (valid_match &&
               !option (OPTCRYPTOPPENCSTRONGKEYS))
        return_key = smime_copy_key (valid_match);
      else
        return_key = NULL;
    }
    else if (trusted_match && !multi_trusted_matches)
    {
      return_key = smime_copy_key (trusted_match);
    }
    else
    {
      return_key = smime_copy_key (smime_select_key (matches, mailbox));
    }

    smime_free_key (&matches);
  }

  return return_key;
}

static smime_key_t *smime_get_key_by_str(char *str, short abilities, short public)
{
  smime_key_t *results, *result;
  smime_key_t *matches = NULL;
  smime_key_t **matches_end = &matches;
  smime_key_t *match;
  smime_key_t *return_key = NULL;

  if (! str)
    return NULL;

  results = smime_get_candidates(str, public);
  for (result = results; result; result = result->next)
  {
    if (abilities && !(result->flags & abilities))
    {
      continue;
    }

    if ((mutt_strcasecmp (str, result->hash) == 0) ||
        mutt_stristr(result->email, str) ||
        mutt_stristr(result->label, str))
    {
      match = smime_copy_key (result);
      *matches_end = match;
      matches_end = &match->next;
    }
  }

  smime_free_key (&results);

  if (matches)
  {
    return_key = smime_copy_key (smime_select_key (matches, str));
    smime_free_key (&matches);
  }

  return return_key;
}


smime_key_t *smime_ask_for_key(char *prompt, short abilities, short public)
{
  smime_key_t *key;
  char resp[SHORT_STRING];

  if (!prompt) prompt = _("Enter keyID: ");

  mutt_clear_error ();

  FOREVER
  {
    resp[0] = 0;
    if (mutt_get_field (prompt, resp, sizeof (resp), MUTT_CLEAR) != 0)
      return NULL;

    if ((key = smime_get_key_by_str (resp, abilities, public)))
      return key;

    BEEP ();
  }
}



/*
  This sets the '*ToUse' variables for an upcoming decryption, where
  the required key is different from SmimeDefaultKey.
*/

void _smime_getkeys (char *mailbox)
{
  smime_key_t *key = NULL;
  const char *k = NULL;
  char buf[STRING];
  size_t smime_keys_len;

  smime_keys_len = mutt_strlen (SmimeKeys);
  key = smime_get_key_by_addr (mailbox, KEYFLAG_CANENCRYPT, 0, 0);

  if (!key)
  {
    snprintf(buf, sizeof(buf), _("Enter keyID for %s: "),
	     mailbox);
    key = smime_ask_for_key (buf, KEYFLAG_CANENCRYPT, 0);
  }

  k = key ? key->hash : NONULL (SmimeDefaultKey);

  /* if the key is different from last time */
  if ((mutt_buffer_len (SmimeKeyToUse) <= smime_keys_len) ||
      mutt_strcasecmp (k, SmimeKeyToUse->data + smime_keys_len + 1))
  {
    smime_void_passphrase ();
    mutt_buffer_printf (SmimeKeyToUse, "%s/%s", NONULL(SmimeKeys), k);
    mutt_buffer_printf (SmimeCertToUse, "%s/%s", NONULL(SmimeCertificates), k);
  }

  smime_free_key (&key);
}

void smime_getkeys (ENVELOPE *env)
{
  ADDRESS *t;
  int found = 0;

  if (option (OPTSDEFAULTDECRYPTKEY) && SmimeDefaultKey)
  {
    mutt_buffer_printf (SmimeKeyToUse, "%s/%s",
	      NONULL (SmimeKeys), SmimeDefaultKey);

    mutt_buffer_printf (SmimeCertToUse, "%s/%s",
	      NONULL(SmimeCertificates), SmimeDefaultKey);

    return;
  }

  for (t = env->to; !found && t; t = t->next)
    if (mutt_addr_is_user (t))
    {
      found = 1;
      _smime_getkeys (t->mailbox);
    }
  for (t = env->cc; !found && t; t = t->next)
    if (mutt_addr_is_user (t))
    {
      found = 1;
      _smime_getkeys (t->mailbox);
    }
  if (!found && (t = mutt_default_from()))
  {
    _smime_getkeys (t->mailbox);
    rfc822_free_address (&t);
  }
}

/* This routine attempts to find the keyids of the recipients of a message.
 * It returns NULL if any of the keys can not be found.
 * If oppenc_mode is true, only keys that can be determined without
 * prompting will be used.
 */

char *smime_findKeys (ADDRESS *adrlist, int oppenc_mode)
{
  smime_key_t *key = NULL;
  char *keyID, *keylist = NULL;
  size_t keylist_size = 0;
  size_t keylist_used = 0;
  ADDRESS *p, *q;

  for (p = adrlist; p ; p = p->next)
  {
    char buf[LONG_STRING];

    q = p;

    key = smime_get_key_by_addr (q->mailbox, KEYFLAG_CANENCRYPT, 1, oppenc_mode);
    if ((key == NULL) && (! oppenc_mode))
    {
      snprintf(buf, sizeof(buf),
	       _("Enter keyID for %s: "),
	       q->mailbox);
      key = smime_ask_for_key (buf, KEYFLAG_CANENCRYPT, 1);
    }
    if (!key)
    {
      if (! oppenc_mode)
        mutt_message (_("No (valid) certificate found for %s."), q->mailbox);
      FREE (&keylist);
      return NULL;
    }

    keyID = key->hash;
    keylist_size += mutt_strlen (keyID) + 2;
    safe_realloc (&keylist, keylist_size);
    sprintf (keylist + keylist_used, "%s%s", keylist_used ? " " : "", keyID);	/* __SPRINTF_CHECKED__ */
    keylist_used = mutt_strlen (keylist);

    smime_free_key (&key);
  }
  return (keylist);
}






static int smime_handle_cert_email (char *certificate, char *mailbox,
                                    int copy, char ***buffer, int *num)
{
  FILE *fpout = NULL, *fperr = NULL;
  BUFFER *tmpfname;
  char email[STRING];
  int ret = -1, count = 0;
  pid_t thepid;
  size_t len = 0;

  tmpfname = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tmpfname);
  if ((fperr = safe_fopen (mutt_b2s (tmpfname), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tmpfname));
    mutt_buffer_pool_release (&tmpfname);
    return 1;
  }
  mutt_unlink (mutt_b2s (tmpfname));

  mutt_buffer_mktemp (tmpfname);
  if ((fpout = safe_fopen (mutt_b2s (tmpfname), "w+")) == NULL)
  {
    safe_fclose (&fperr);
    mutt_perror (mutt_b2s (tmpfname));
    mutt_buffer_pool_release (&tmpfname);
    return 1;
  }
  mutt_unlink (mutt_b2s (tmpfname));
  mutt_buffer_pool_release (&tmpfname);

  if ((thepid =  smime_invoke (NULL, NULL, NULL,
			       -1, fileno (fpout), fileno (fperr),
			       certificate, NULL, NULL, NULL, NULL, NULL, NULL,
			       SmimeGetCertEmailCommand))== -1)
  {
    mutt_message (_("Error: unable to create OpenSSL subprocess!"));
    safe_fclose (&fperr);
    safe_fclose (&fpout);
    return 1;
  }

  mutt_wait_filter (thepid);

  fflush (fpout);
  rewind (fpout);
  fflush (fperr);
  rewind (fperr);


  while ((fgets (email, sizeof (email), fpout)))
  {
    len = mutt_strlen (email);
    if (len && (email[len - 1] == '\n'))
      email[len - 1] = '\0';
    if (mutt_strncasecmp (email, mailbox, mutt_strlen (mailbox)) == 0)
      ret=1;

    ret = ret < 0 ? 0 : ret;
    count++;
  }

  if (ret == -1)
  {
    mutt_endwin(NULL);
    mutt_copy_stream (fperr, stdout);
    mutt_any_key_to_continue (_("Error: unable to create OpenSSL subprocess!"));
    ret = 1;
  }
  else if (!ret)
    ret = 1;
  else ret = 0;

  if (copy && buffer && num)
  {
    (*num) = count;
    *buffer =  safe_calloc(sizeof(char*), count);
    count = 0;

    rewind (fpout);
    while ((fgets (email, sizeof (email), fpout)))
    {
      len = mutt_strlen (email);
      if (len && (email[len - 1] == '\n'))
        email[len - 1] = '\0';
      (*buffer)[count] = safe_calloc(1, mutt_strlen (email) + 1);
      strncpy((*buffer)[count], email, mutt_strlen (email));
      count++;
    }
  }
  else if (copy) ret = 2;

  safe_fclose (&fpout);
  safe_fclose (&fperr);

  return ret;
}



static char *smime_extract_certificate (const char *infile)
{
  FILE *fperr = NULL, *fppk7out = NULL, *fpcertfile = NULL;
  BUFFER *tmpfname = NULL, *pk7out = NULL, *certfile = NULL;
  char *retval = NULL;
  pid_t thepid;
  int empty;

  tmpfname = mutt_buffer_pool_get ();
  pk7out = mutt_buffer_pool_get ();
  certfile = mutt_buffer_pool_get ();

  mutt_buffer_mktemp (tmpfname);
  if ((fperr = safe_fopen (mutt_b2s (tmpfname), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tmpfname));
    goto cleanup;
  }
  mutt_unlink (mutt_b2s (tmpfname));

  mutt_buffer_mktemp (pk7out);
  if ((fppk7out = safe_fopen (mutt_b2s (pk7out), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (pk7out));
    goto cleanup;
  }

  /* Step 1: Convert the signature to a PKCS#7 structure, as we can't
     extract the full set of certificates directly.
  */
  if ((thepid =  smime_invoke (NULL, NULL, NULL,
			       -1, fileno (fppk7out), fileno (fperr),
			       infile, NULL, NULL, NULL, NULL, NULL, NULL,
			       SmimePk7outCommand))== -1)
  {
    mutt_any_key_to_continue (_("Error: unable to create OpenSSL subprocess!"));
    goto cleanup;
  }

  mutt_wait_filter (thepid);


  fflush (fppk7out);
  rewind (fppk7out);
  fflush (fperr);
  rewind (fperr);
  empty = (fgetc (fppk7out) == EOF);
  if (empty)
  {
    mutt_perror (mutt_b2s (pk7out));
    mutt_copy_stream (fperr, stdout);
    goto cleanup;
  }
  safe_fclose (&fppk7out);


  mutt_buffer_mktemp (certfile);
  if ((fpcertfile = safe_fopen (mutt_b2s (certfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (certfile));
    mutt_unlink (mutt_b2s (pk7out));
    goto cleanup;
  }

  /* Step 2: Extract the certificates from a PKCS#7 structure.
   */
  if ((thepid =  smime_invoke (NULL, NULL, NULL,
			       -1, fileno (fpcertfile), fileno (fperr),
			       mutt_b2s (pk7out), NULL, NULL, NULL, NULL, NULL, NULL,
			       SmimeGetCertCommand))== -1)
  {
    mutt_any_key_to_continue (_("Error: unable to create OpenSSL subprocess!"));
    mutt_unlink (mutt_b2s (pk7out));
    goto cleanup;
  }

  mutt_wait_filter (thepid);

  mutt_unlink (mutt_b2s (pk7out));

  fflush (fpcertfile);
  rewind (fpcertfile);
  fflush (fperr);
  rewind (fperr);
  empty =  (fgetc (fpcertfile) == EOF);
  if (empty)
  {
    mutt_copy_stream (fperr, stdout);
    goto cleanup;
  }

  safe_fclose (&fpcertfile);

  retval = safe_strdup (mutt_b2s (certfile));

cleanup:
  safe_fclose (&fperr);
  if (fppk7out)
  {
    safe_fclose (&fppk7out);
    mutt_unlink (mutt_b2s (pk7out));
  }
  if (fpcertfile)
  {
    safe_fclose (&fpcertfile);
    mutt_unlink (mutt_b2s (certfile));
  }
  mutt_buffer_pool_release (&tmpfname);
  mutt_buffer_pool_release (&pk7out);
  mutt_buffer_pool_release (&certfile);
  return retval;
}

static char *smime_extract_signer_certificate (const char *infile)
{
  FILE *fpout = NULL, *fperr = NULL;
  char *retval = NULL;
  BUFFER *tmpfname = NULL, *certfile = NULL;
  pid_t thepid;
  int empty;

  tmpfname = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tmpfname);
  if ((fperr = safe_fopen (mutt_b2s (tmpfname), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tmpfname));
    goto cleanup;
  }
  mutt_unlink (mutt_b2s (tmpfname));
  mutt_buffer_pool_release (&tmpfname);

  certfile = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (certfile);
  if ((fpout = safe_fopen (mutt_b2s (certfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (certfile));
    goto cleanup;
  }

  /* Extract signer's certificate
   */
  if ((thepid =  smime_invoke (NULL, NULL, NULL,
			       -1, -1, fileno (fperr),
			       infile, NULL, NULL, NULL, NULL,
                               mutt_b2s (certfile), NULL,
			       SmimeGetSignerCertCommand))== -1)
  {
    mutt_any_key_to_continue (_("Error: unable to create OpenSSL subprocess!"));
    goto cleanup;
  }

  mutt_wait_filter (thepid);

  fflush (fpout);
  rewind (fpout);
  fflush (fperr);
  rewind (fperr);
  empty =  (fgetc (fpout) == EOF);
  if (empty)
  {
    mutt_endwin (NULL);
    mutt_copy_stream (fperr, stdout);
    mutt_any_key_to_continue (NULL);
    goto cleanup;
  }

  safe_fclose (&fpout);
  retval = safe_strdup (mutt_b2s (certfile));

cleanup:
  safe_fclose (&fperr);
  if (fpout)
  {
    safe_fclose (&fpout);
    mutt_unlink (mutt_b2s (certfile));
  }
  mutt_buffer_pool_release (&tmpfname);
  mutt_buffer_pool_release (&certfile);
  return retval;
}




/* Add a certificate and update index file (externally). */

void smime_invoke_import (const char *infile, const char *mailbox)
{
  BUFFER *tmpfname;
  char *certfile = NULL, buf[STRING];
  FILE *smimein=NULL, *fpout = NULL, *fperr = NULL;
  pid_t thepid=-1;

  tmpfname = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tmpfname);
  if ((fperr = safe_fopen (mutt_b2s (tmpfname), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tmpfname));
    mutt_buffer_pool_release (&tmpfname);
    return;
  }
  mutt_unlink (mutt_b2s (tmpfname));

  mutt_buffer_mktemp (tmpfname);
  if ((fpout = safe_fopen (mutt_b2s (tmpfname), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tmpfname));
    safe_fclose (&fperr);
    mutt_buffer_pool_release (&tmpfname);
    return;
  }
  mutt_unlink (mutt_b2s (tmpfname));
  mutt_buffer_pool_release (&tmpfname);


  buf[0] = '\0';
  if (option (OPTASKCERTLABEL))
    mutt_get_field (_("Label for certificate: "), buf, sizeof (buf), 0);

  mutt_endwin (NULL);
  if ((certfile = smime_extract_certificate(infile)))
  {
    mutt_endwin (NULL);

    if ((thepid =  smime_invoke (&smimein, NULL, NULL,
				 -1, fileno(fpout), fileno(fperr),
				 certfile, NULL, NULL, NULL, NULL, NULL, NULL,
				 SmimeImportCertCommand))== -1)
    {
      mutt_message (_("Error: unable to create OpenSSL subprocess!"));
      return;
    }
    fputs (buf, smimein);
    fputc ('\n', smimein);
    safe_fclose (&smimein);

    mutt_wait_filter (thepid);

    mutt_unlink (certfile);
    FREE (&certfile);
  }

  fflush (fpout);
  rewind (fpout);
  fflush (fperr);
  rewind (fperr);

  mutt_copy_stream (fpout, stdout);
  mutt_copy_stream (fperr, stdout);

  safe_fclose (&fpout);
  safe_fclose (&fperr);

}



int smime_verify_sender(HEADER *h)
{
  char *mbox = NULL, *certfile;
  BUFFER *tempfname;
  FILE *fpout;
  int retval=1;

  tempfname = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tempfname);
  if (!(fpout = safe_fopen (mutt_b2s (tempfname), "w")))
  {
    mutt_perror (mutt_b2s (tempfname));
    goto cleanup;
  }

  if (h->security & ENCRYPT)
    mutt_copy_message (fpout, Context, h,
		       MUTT_CM_DECODE_CRYPT & MUTT_CM_DECODE_SMIME,
		       CH_MIME|CH_WEED|CH_NONEWLINE);
  else
    mutt_copy_message (fpout, Context, h, 0, 0);

  fflush(fpout);
  safe_fclose (&fpout);

  if (h->env->from)
  {
    h->env->from = mutt_expand_aliases (h->env->from);
    mbox = h->env->from->mailbox;
  }
  else if (h->env->sender)
  {
    h->env->sender = mutt_expand_aliases (h->env->sender);
    mbox = h->env->sender->mailbox;
  }

  if (mbox)
  {
    if ((certfile = smime_extract_signer_certificate (mutt_b2s (tempfname))))
    {
      mutt_unlink(mutt_b2s (tempfname));
      if (smime_handle_cert_email (certfile, mbox, 0, NULL, NULL))
      {
	if (isendwin())
          mutt_any_key_to_continue(NULL);
      }
      else
	retval = 0;
      mutt_unlink(certfile);
      FREE (&certfile);
    }
    else
      mutt_any_key_to_continue(_("no certfile"));
  }
  else
    mutt_any_key_to_continue(_("no mbox"));

  mutt_unlink(mutt_b2s (tempfname));

cleanup:
  mutt_buffer_pool_release (&tempfname);
  return retval;
}









/*
 *    Creating S/MIME - bodies.
 */




static
pid_t smime_invoke_encrypt (FILE **smimein, FILE **smimeout, FILE **smimeerr,
			    int smimeinfd, int smimeoutfd, int smimeerrfd,
			    const char *fname, const char *uids)
{
  return smime_invoke (smimein, smimeout, smimeerr,
		       smimeinfd, smimeoutfd, smimeerrfd,
		       fname, NULL, SmimeCryptAlg, NULL, NULL, uids, NULL,
		       SmimeEncryptCommand);
}


static
pid_t smime_invoke_sign (FILE **smimein, FILE **smimeout, FILE **smimeerr,
			 int smimeinfd, int smimeoutfd, int smimeerrfd,
			 const char *fname)
{
  return smime_invoke (smimein, smimeout, smimeerr, smimeinfd, smimeoutfd,
		       smimeerrfd, fname, NULL, NULL, SmimeDigestAlg,
                       mutt_b2s (SmimeKeyToUse),
		       mutt_b2s (SmimeCertToUse), mutt_b2s (SmimeIntermediateToUse),
		       SmimeSignCommand);
}




BODY *smime_build_smime_entity (BODY *a, char *certlist)
{
  char buf[LONG_STRING], certfile[LONG_STRING];
  BUFFER *tempfile = NULL, *smimeerrfile = NULL, *smimeinfile = NULL;
  char *cert_start, *cert_end;
  FILE *smimein = NULL, *smimeerr = NULL, *fpout = NULL, *fptmp = NULL;
  BODY *t = NULL;
  int err = 0, empty;
  size_t off;
  pid_t thepid;

  tempfile = mutt_buffer_pool_get ();
  smimeerrfile = mutt_buffer_pool_get ();
  smimeinfile = mutt_buffer_pool_get ();

  mutt_buffer_mktemp (tempfile);
  if ((fpout = safe_fopen (mutt_b2s (tempfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tempfile));
    goto cleanup;
  }

  mutt_buffer_mktemp (smimeerrfile);
  if ((smimeerr = safe_fopen (mutt_b2s (smimeerrfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (smimeerrfile));
    goto cleanup;
  }
  mutt_unlink (mutt_b2s (smimeerrfile));

  mutt_buffer_mktemp (smimeinfile);
  if ((fptmp = safe_fopen (mutt_b2s (smimeinfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (smimeinfile));
    goto cleanup;
  }

  *certfile = '\0';
  for (cert_start = certlist; cert_start; cert_start = cert_end)
  {
    if ((cert_end = strchr (cert_start, ' ')))
      *cert_end = '\0';
    if (*cert_start)
    {
      off = mutt_strlen (certfile);
      snprintf (certfile+off, sizeof (certfile)-off, "%s%s/%s",
                off ? " " : "", NONULL(SmimeCertificates), cert_start);
    }
    if (cert_end)
      *cert_end++ = ' ';
  }

  /* write a MIME entity */
  mutt_write_mime_header (a, fptmp);
  fputc ('\n', fptmp);
  mutt_write_mime_body (a, fptmp);
  safe_fclose (&fptmp);

  if ((thepid =
       smime_invoke_encrypt (&smimein, NULL, NULL, -1,
			     fileno (fpout), fileno (smimeerr),
			     mutt_b2s (smimeinfile), certfile)) == -1)
  {
    mutt_unlink (mutt_b2s (smimeinfile));
    goto cleanup;
  }

  safe_fclose (&smimein);

  mutt_wait_filter (thepid);
  mutt_unlink (mutt_b2s (smimeinfile));

  fflush (fpout);
  rewind (fpout);
  empty = (fgetc (fpout) == EOF);
  safe_fclose (&fpout);

  fflush (smimeerr);
  rewind (smimeerr);
  while (fgets (buf, sizeof (buf) - 1, smimeerr) != NULL)
  {
    err = 1;
    fputs (buf, stdout);
  }
  safe_fclose (&smimeerr);

  /* pause if there is any error output from SMIME */
  if (err)
    mutt_any_key_to_continue (NULL);

  if (empty)
  {
    /* fatal error while trying to encrypt message */
    if (!err) mutt_any_key_to_continue _("No output from OpenSSL...");
    mutt_unlink (mutt_b2s (tempfile));
    goto cleanup;
  }

  t = mutt_new_body ();
  t->type = TYPEAPPLICATION;
  t->subtype = safe_strdup ("x-pkcs7-mime");
  mutt_set_parameter ("name", "smime.p7m", &t->parameter);
  mutt_set_parameter ("smime-type", "enveloped-data", &t->parameter);
  t->encoding = ENCBASE64;  /* The output of OpenSSL SHOULD be binary */
  t->use_disp = 1;
  t->disposition = DISPATTACH;
  t->d_filename = safe_strdup ("smime.p7m");
  t->filename = safe_strdup (mutt_b2s (tempfile));
  t->unlink = 1; /*delete after sending the message */
  t->parts=0;
  t->next=0;

cleanup:
  if (fpout)
  {
    safe_fclose (&fpout);
    mutt_unlink (mutt_b2s (tempfile));
  }
  safe_fclose (&smimeerr);
  if (fptmp)
  {
    safe_fclose (&fptmp);
    mutt_unlink (mutt_b2s (smimeinfile));
  }
  mutt_buffer_pool_release (&tempfile);
  mutt_buffer_pool_release (&smimeerrfile);
  mutt_buffer_pool_release (&smimeinfile);

  return (t);
}


/* The openssl -md doesn't want hyphens:
 *   md5, sha1,  sha224,  sha256,  sha384,  sha512
 * However, the micalg does:
 *   md5, sha-1, sha-224, sha-256, sha-384, sha-512
 */
static char *openssl_md_to_smime_micalg(char *md)
{
  char *micalg;
  size_t l;

  if (!md)
    return 0;

  if (mutt_strncasecmp ("sha", md, 3) == 0)
  {
    l = strlen (md) + 2;
    micalg = (char *)safe_malloc (l);
    snprintf (micalg, l, "sha-%s", md +3);
  }
  else
  {
    micalg = safe_strdup (md);
  }

  return micalg;
}



BODY *smime_sign_message (BODY *a )
{
  BODY *t, *retval = NULL;
  char buffer[LONG_STRING];
  BUFFER *filetosign = NULL, *signedfile = NULL;
  FILE *smimein = NULL, *smimeout = NULL, *smimeerr = NULL, *sfp = NULL;
  int err = 0;
  int empty = 0;
  pid_t thepid;
  char *signas;
  smime_key_t *signas_key;
  char *intermediates;
  char *micalg;

  signas = SmimeSignAs ? SmimeSignAs : SmimeDefaultKey;
  if (!signas)
  {
    mutt_error _("Can't sign: No key specified. Use Sign As.");
    return NULL;
  }

  convert_to_7bit (a); /* Signed data _must_ be in 7-bit format. */

  filetosign = mutt_buffer_pool_get ();
  signedfile = mutt_buffer_pool_get ();

  mutt_buffer_mktemp (filetosign);
  if ((sfp = safe_fopen (mutt_b2s (filetosign), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (filetosign));
    goto cleanup;
  }

  mutt_buffer_mktemp (signedfile);
  if ((smimeout = safe_fopen (mutt_b2s (signedfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (signedfile));
    goto cleanup;
  }

  mutt_write_mime_header (a, sfp);
  fputc ('\n', sfp);
  mutt_write_mime_body (a, sfp);
  safe_fclose (&sfp);



  mutt_buffer_printf (SmimeKeyToUse, "%s/%s",
            NONULL(SmimeKeys), signas);

  mutt_buffer_printf (SmimeCertToUse, "%s/%s",
            NONULL(SmimeCertificates), signas);

  signas_key = smime_get_key_by_hash (signas, 1);
  if ((! signas_key) ||
      (! mutt_strcmp ("?", signas_key->issuer)))
    intermediates = signas; /* so openssl won't complain in any case */
  else
    intermediates = signas_key->issuer;

  mutt_buffer_printf (SmimeIntermediateToUse, "%s/%s",
            NONULL(SmimeCertificates), intermediates);

  smime_free_key (&signas_key);



  if ((thepid = smime_invoke_sign (&smimein, NULL, &smimeerr,
                                   -1, fileno (smimeout), -1, mutt_b2s (filetosign))) == -1)
  {
    mutt_perror _("Can't open OpenSSL subprocess!");
    mutt_unlink (mutt_b2s (filetosign));
    goto cleanup;
  }
  fputs (SmimePass, smimein);
  fputc ('\n', smimein);
  safe_fclose (&smimein);


  mutt_wait_filter (thepid);

  /* check for errors from OpenSSL */
  err = 0;
  fflush (smimeerr);
  rewind (smimeerr);
  while (fgets (buffer, sizeof (buffer) - 1, smimeerr) != NULL)
  {
    err = 1;
    fputs (buffer, stdout);
  }
  safe_fclose (&smimeerr);


  fflush (smimeout);
  rewind (smimeout);
  empty = (fgetc (smimeout) == EOF);
  safe_fclose (&smimeout);

  mutt_unlink (mutt_b2s (filetosign));


  if (err)
    mutt_any_key_to_continue (NULL);

  if (empty)
  {
    mutt_any_key_to_continue _("No output from OpenSSL...");
    mutt_unlink (mutt_b2s (signedfile));
    goto cleanup;
  }

  t = mutt_new_body ();
  t->type = TYPEMULTIPART;
  t->subtype = safe_strdup ("signed");
  t->encoding = ENC7BIT;
  t->use_disp = 0;
  t->disposition = DISPINLINE;

  mutt_generate_boundary (&t->parameter);

  micalg = openssl_md_to_smime_micalg (SmimeDigestAlg);
  mutt_set_parameter ("micalg", micalg, &t->parameter);
  FREE (&micalg);

  mutt_set_parameter ("protocol", "application/x-pkcs7-signature",
                      &t->parameter);

  t->parts = a;
  retval = t;

  t->parts->next = mutt_new_body ();
  t = t->parts->next;
  t->type = TYPEAPPLICATION;
  t->subtype = safe_strdup ("x-pkcs7-signature");
  t->filename = safe_strdup (mutt_b2s (signedfile));
  t->d_filename = safe_strdup ("smime.p7s");
  t->use_disp = 1;
  t->disposition = DISPATTACH;
  t->encoding = ENCBASE64;
  t->unlink = 1; /* ok to remove this file after sending. */

cleanup:
  if (sfp)
  {
    safe_fclose (&sfp);
    mutt_unlink (mutt_b2s (filetosign));
  }
  if (smimeout)
  {
    safe_fclose (&smimeout);
    mutt_unlink (mutt_b2s (signedfile));
  }
  mutt_buffer_pool_release (&filetosign);
  mutt_buffer_pool_release (&signedfile);
  return (retval);
}






/*
 *    Handling S/MIME - bodies.
 */






static
pid_t smime_invoke_verify (FILE **smimein, FILE **smimeout, FILE **smimeerr,
			   int smimeinfd, int smimeoutfd, int smimeerrfd,
			   const char *fname, const char *sig_fname, int opaque)
{
  return smime_invoke (smimein, smimeout, smimeerr, smimeinfd, smimeoutfd,
		       smimeerrfd, fname, sig_fname, NULL, NULL, NULL, NULL, NULL,
		       (opaque ? SmimeVerifyOpaqueCommand : SmimeVerifyCommand));
}


static
pid_t smime_invoke_decrypt (FILE **smimein, FILE **smimeout, FILE **smimeerr,
			    int smimeinfd, int smimeoutfd, int smimeerrfd,
			    const char *fname)
{
  return smime_invoke (smimein, smimeout, smimeerr, smimeinfd, smimeoutfd,
		       smimeerrfd, fname, NULL, NULL, NULL, mutt_b2s (SmimeKeyToUse),
		       mutt_b2s (SmimeCertToUse), NULL, SmimeDecryptCommand);
}



int smime_verify_one (BODY *sigbdy, STATE *s, const char *tempfile)
{
  BUFFER *signedfile = NULL, *smimeerrfile = NULL;
  FILE *smimeout=NULL, *smimeerr=NULL;
  pid_t thepid;
  int badsig = -1;
  FILE *s_fpout_save;
  char *s_prefix_save;

  signedfile = mutt_buffer_pool_get ();
  smimeerrfile = mutt_buffer_pool_get ();

  mutt_buffer_printf (signedfile, "%s.sig", tempfile);

  /* decode to a tempfile, saving the original destination */
  s_fpout_save = s->fpout;
  if ((s->fpout = safe_fopen (mutt_b2s (signedfile), "w")) == NULL)
  {
    mutt_perror (mutt_b2s (signedfile));
    s->fpout = s_fpout_save;
    goto signedfile_cleanup;
  }

  /* if we are decoding binary bodies, we don't want to prefix each
   * line with the prefix or else the data will get corrupted.
   */
  s_prefix_save = s->prefix;
  s->prefix = NULL;

  mutt_decode_attachment (sigbdy, s);

  s->prefix = s_prefix_save;
  safe_fclose (&s->fpout);
  s->fpout = s_fpout_save;

  mutt_buffer_mktemp (smimeerrfile);
  if (!(smimeerr = safe_fopen (mutt_b2s (smimeerrfile), "w+")))
  {
    mutt_perror (mutt_b2s (smimeerrfile));
    goto errfile_cleanup;
  }

  crypt_current_time (s, "OpenSSL");

  if ((thepid = smime_invoke_verify (NULL, &smimeout, NULL,
                                     -1, -1, fileno (smimeerr),
                                     tempfile, mutt_b2s (signedfile), 0)) != -1)
  {
    fflush (smimeout);
    safe_fclose (&smimeout);

    if (mutt_wait_filter (thepid))
      badsig = -1;
    else
    {
      char *line = NULL;
      int lineno = 0;
      size_t linelen;

      fflush (smimeerr);
      rewind (smimeerr);

      line = mutt_read_line (line, &linelen, smimeerr, &lineno, 0);
      if (linelen && !ascii_strcasecmp (line, "verification successful"))
	badsig = 0;

      FREE (&line);
    }
  }

  fflush (smimeerr);
  rewind (smimeerr);
  mutt_copy_stream (smimeerr, s->fpout);
  safe_fclose (&smimeerr);

  state_attach_puts (_("[-- End of OpenSSL output --]\n\n"), s);

  mutt_unlink (mutt_b2s (smimeerrfile));

errfile_cleanup:
  mutt_unlink (mutt_b2s (signedfile));

signedfile_cleanup:
  mutt_buffer_pool_release (&signedfile);
  mutt_buffer_pool_release (&smimeerrfile);
  return badsig;
}





/*
  This handles application/pkcs7-mime which can either be a signed
  or an encrypted message.
*/

static BODY *smime_handle_entity (BODY *m, STATE *s, FILE *outFile)
{
  size_t len=0;
  int c;
  char buf[HUGE_STRING];
  BUFFER *outfile = NULL, *errfile = NULL, *tmpfname = NULL;
  BUFFER *tmptmpfname = NULL;
  FILE *smimeout = NULL, *smimein=NULL, *smimeerr=NULL;
  FILE *tmpfp=NULL, *tmpfp_buffer=NULL, *fpout=NULL;
  struct stat info;
  BODY *p=NULL;
  pid_t thepid=-1;
  unsigned int type = mutt_is_application_smime (m);

  if (!(type & APPLICATION_SMIME)) return NULL;

  /* Because of the mutt_body_handler() we avoid the buffer pool. */
  outfile = mutt_buffer_new ();
  errfile = mutt_buffer_new ();
  tmpfname = mutt_buffer_new ();

  mutt_buffer_mktemp (outfile);
  if ((smimeout = safe_fopen (mutt_b2s (outfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (outfile));
    goto cleanup;
  }

  mutt_buffer_mktemp (errfile);
  if ((smimeerr = safe_fopen (mutt_b2s (errfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (errfile));
    goto cleanup;
  }
  mutt_unlink (mutt_b2s (errfile));

  mutt_buffer_mktemp (tmpfname);
  if ((tmpfp = safe_fopen (mutt_b2s (tmpfname), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tmpfname));
    goto cleanup;
  }

  fseeko (s->fpin, m->offset, SEEK_SET);

  mutt_copy_bytes (s->fpin, tmpfp,  m->length);

  fflush (tmpfp);
  safe_fclose (&tmpfp);

  if ((type & ENCRYPT) &&
      (thepid = smime_invoke_decrypt (&smimein, NULL, NULL, -1,
				      fileno (smimeout),  fileno (smimeerr),
                                      mutt_b2s (tmpfname))) == -1)
  {
    mutt_unlink (mutt_b2s (tmpfname));
    if (s->flags & MUTT_DISPLAY)
      state_attach_puts (_("[-- Error: unable to create OpenSSL subprocess! --]\n"), s);
    goto cleanup;
  }
  else if ((type & SIGNOPAQUE) &&
	   (thepid = smime_invoke_verify (&smimein, NULL, NULL, -1,
					  fileno (smimeout), fileno (smimeerr), NULL,
					  mutt_b2s (tmpfname), SIGNOPAQUE)) == -1)
  {
    mutt_unlink (mutt_b2s (tmpfname));
    if (s->flags & MUTT_DISPLAY)
      state_attach_puts (_("[-- Error: unable to create OpenSSL subprocess! --]\n"), s);
    goto cleanup;
  }


  if (type & ENCRYPT)
  {
    if (!smime_valid_passphrase ())
      smime_void_passphrase ();
    fputs (SmimePass, smimein);
    fputc ('\n', smimein);
  }

  safe_fclose (&smimein);

  mutt_wait_filter (thepid);
  mutt_unlink (mutt_b2s (tmpfname));


  if (s->flags & MUTT_DISPLAY)
  {
    fflush (smimeerr);
    rewind (smimeerr);

    if ((c = fgetc (smimeerr)) != EOF)
    {
      ungetc (c, smimeerr);

      crypt_current_time (s, "OpenSSL");
      mutt_copy_stream (smimeerr, s->fpout);
      state_attach_puts (_("[-- End of OpenSSL output --]\n\n"), s);
    }

    if (type & ENCRYPT)
      state_attach_puts (_("[-- The following data is S/MIME"
                           " encrypted --]\n"), s);
    else
      state_attach_puts (_("[-- The following data is S/MIME signed --]\n"), s);
  }

  fflush (smimeout);
  rewind (smimeout);

  if (type & ENCRYPT)
  {
    /* void the passphrase, even if that wasn't the problem */
    if (fgetc (smimeout) == EOF)
    {
      mutt_error _("Decryption failed");
      smime_void_passphrase ();
    }
    rewind (smimeout);
  }

  if (outFile) fpout = outFile;
  else
  {
    tmptmpfname = mutt_buffer_new ();
    mutt_buffer_mktemp (tmptmpfname);
    if ((fpout = safe_fopen (mutt_b2s (tmptmpfname), "w+")) == NULL)
    {
      mutt_perror (mutt_b2s (tmptmpfname));
      goto cleanup;
    }
  }
  while (fgets (buf, sizeof (buf) - 1, smimeout) != NULL)
  {
    len = mutt_strlen (buf);
    if (len > 1 && buf[len - 2] == '\r')
    {
      buf[len-2] = '\n';
      buf[len-1] = '\0';
    }
    fputs (buf, fpout);
  }
  fflush (fpout);
  rewind (fpout);

  if ((p = mutt_read_mime_header (fpout, 0)) != NULL)
  {
    fstat (fileno (fpout), &info);
    p->length = info.st_size - p->offset;

    mutt_parse_part (fpout, p);

    if (s->flags & MUTT_DISPLAY)
    {
      mutt_protected_headers_handler (p, s);
    }

    /* Store any protected headers in the parent so they can be
     * accessed for index updates after the handler recursion is done.
     * This is done before the handler to prevent a nested encrypted
     * handler from freeing the headers. */
    mutt_free_envelope (&m->mime_headers);
    m->mime_headers = p->mime_headers;
    p->mime_headers = NULL;

    if (s->fpout)
    {
      rewind (fpout);
      tmpfp_buffer = s->fpin;
      s->fpin = fpout;
      mutt_body_handler (p, s);
      s->fpin = tmpfp_buffer;
    }

    /* Embedded multipart signed protected headers override the
     * encrypted headers.  We need to do this after the handler so
     * they can be printed in the pager. */
    if (!(type & SMIMESIGN) &&
        mutt_is_multipart_signed (p) &&
        p->parts &&
        p->parts->mime_headers)
    {
      mutt_free_envelope (&m->mime_headers);
      m->mime_headers = p->parts->mime_headers;
      p->parts->mime_headers = NULL;
    }
  }

  safe_fclose (&smimeout);
  mutt_unlink (mutt_b2s (outfile));

  if (!outFile)
  {
    safe_fclose (&fpout);
    mutt_unlink (mutt_b2s (tmptmpfname));
  }
  fpout = NULL;

  if (s->flags & MUTT_DISPLAY)
  {
    if (type & ENCRYPT)
      state_attach_puts (_("\n[-- End of S/MIME encrypted data. --]\n"), s);
    else
      state_attach_puts (_("\n[-- End of S/MIME signed data. --]\n"), s);
  }

  if (type & SIGNOPAQUE)
  {
    char *line = NULL;
    int lineno = 0;
    size_t linelen;

    rewind (smimeerr);

    line = mutt_read_line (line, &linelen, smimeerr, &lineno, 0);
    if (linelen && !ascii_strcasecmp (line, "verification successful"))
      m->goodsig = 1;
    FREE (&line);
  }
  else if (p)
  {
    m->goodsig = p->goodsig;
    m->badsig  = p->badsig;
  }
  safe_fclose (&smimeerr);

cleanup:
  if (smimeout)
  {
    safe_fclose (&smimeout);
    mutt_unlink (mutt_b2s (outfile));
  }
  safe_fclose (&smimeerr);
  safe_fclose (&tmpfp);
  if (!outFile && fpout)
  {
    safe_fclose (&fpout);
    mutt_unlink (mutt_b2s (tmptmpfname));
  }
  mutt_buffer_free (&outfile);
  mutt_buffer_free (&errfile);
  mutt_buffer_free (&tmpfname);
  mutt_buffer_free (&tmptmpfname);
  return (p);
}





int smime_decrypt_mime (FILE *fpin, FILE **fpout, BODY *b, BODY **cur)
{
  BUFFER *tempfile = NULL;
  STATE s;
  LOFF_T tmpoffset = b->offset;
  size_t tmplength = b->length;
  FILE *tmpfp=NULL;
  int rv = -1;

  if (!mutt_is_application_smime (b))
    return -1;

  if (b->parts)
    return -1;

  *fpout = NULL;

  memset (&s, 0, sizeof (s));
  s.fpin = fpin;
  fseeko (s.fpin, b->offset, SEEK_SET);

  tempfile = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tempfile);
  if ((tmpfp = safe_fopen (mutt_b2s (tempfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tempfile));
    goto bail;
  }

  mutt_unlink (mutt_b2s (tempfile));
  s.fpout = tmpfp;
  mutt_decode_attachment (b, &s);
  fflush (tmpfp);
  b->length = ftello (s.fpout);
  b->offset = 0;
  rewind (tmpfp);
  s.fpin = tmpfp;
  s.fpout = 0;

  mutt_buffer_mktemp (tempfile);
  if ((*fpout = safe_fopen (mutt_b2s (tempfile), "w+")) == NULL)
  {
    mutt_perror (mutt_b2s (tempfile));
    goto bail;
  }
  mutt_unlink (mutt_b2s (tempfile));
  mutt_buffer_pool_release (&tempfile);

  if (!(*cur = smime_handle_entity (b, &s, *fpout)))
  {
    goto bail;
  }

  rv = 0;
  (*cur)->goodsig = b->goodsig;
  (*cur)->badsig  = b->badsig;

bail:
  b->length = tmplength;
  b->offset = tmpoffset;
  safe_fclose (&tmpfp);
  if (*fpout)
    rewind (*fpout);
  mutt_buffer_pool_release (&tempfile);

  return rv;
}


int smime_application_smime_handler (BODY *m, STATE *s)
{
  int rv = 1;
  BODY *tattach;

  /* clear out any mime headers before the handler, so they can't be
   * spoofed. */
  mutt_free_envelope (&m->mime_headers);

  tattach = smime_handle_entity (m, s, NULL);
  if (tattach)
  {
    rv = 0;
    mutt_free_body (&tattach);
  }
  return rv;
}

void smime_send_menu (SEND_CONTEXT *sctx)
{
  HEADER *msg;
  smime_key_t *key;
  char *prompt, *letters, *choices;
  int choice;

  msg = sctx->msg;

  if (!(WithCrypto & APPLICATION_SMIME))
    return;

  msg->security |= APPLICATION_SMIME;

  /*
   * Opportunistic encrypt is controlling encryption.
   * NOTE: "Signing" and "Clearing" only adjust the sign bit, so we have different
   *       letter choices for those.
   */
  if (option (OPTCRYPTOPPORTUNISTICENCRYPT) && (msg->security & OPPENCRYPT))
  {
    prompt = _("S/MIME (s)ign, encrypt (w)ith, sign (a)s, (c)lear, or (o)ppenc mode off? ");
    /* L10N: The 'f' is from "forget it", an old undocumented synonym of
       'clear'.  Please use a corresponding letter in your language.
       Alternatively, you may duplicate the letter 'c' is translated to.
       This comment also applies to the two following letter sequences. */
    letters = _("swafco");
    choices = "SwaFCo";
  }
  /*
   * Opportunistic encryption option is set, but is toggled off
   * for this message.
   */
  else if (option (OPTCRYPTOPPORTUNISTICENCRYPT))
  {
    prompt = _("S/MIME (e)ncrypt, (s)ign, encrypt (w)ith, sign (a)s, (b)oth, (c)lear, or (o)ppenc mode? ");
    letters = _("eswabfco");
    choices = "eswabfcO";
  }
  /*
   * Opportunistic encryption is unset
   */
  else
  {
    prompt = _("S/MIME (e)ncrypt, (s)ign, encrypt (w)ith, sign (a)s, (b)oth, or (c)lear? ");
    letters = _("eswabfc");
    choices = "eswabfc";
  }


  choice = mutt_multi_choice (prompt, letters);
  if (choice > 0)
  {
    switch (choices[choice - 1])
    {
      case 'e': /* (e)ncrypt */
        msg->security |= ENCRYPT;
        msg->security &= ~SIGN;
        break;

      case 'w': /* encrypt (w)ith */
      {
        msg->security |= ENCRYPT;
        do
        {
          /* I use "dra" because "123" is recognized anyway */
          switch (mutt_multi_choice (_("Choose algorithm family:"
                                       " 1: DES, 2: RC2, 3: AES,"
                                       " or (c)lear? "),
                                     _("drac")))
          {
            case 1:
              switch (choice = mutt_multi_choice (_("1: DES, 2: Triple-DES "),
                                                  _("dt")))
              {
                case 1:
                  mutt_str_replace (&sctx->smime_crypt_alg, "des");
                  break;
                case 2:
                  mutt_str_replace (&sctx->smime_crypt_alg, "des3");
                  break;
              }
              break;

            case 2:
              switch (choice = mutt_multi_choice (_("1: RC2-40, 2: RC2-64, 3: RC2-128 "),
                                                  _("468")))
              {
                case 1:
                  mutt_str_replace (&sctx->smime_crypt_alg, "rc2-40");
                  break;
                case 2:
                  mutt_str_replace (&sctx->smime_crypt_alg, "rc2-64");
                  break;
                case 3:
                  mutt_str_replace (&sctx->smime_crypt_alg, "rc2-128");
                  break;
              }
              break;

            case 3:
              switch (choice = mutt_multi_choice (_("1: AES128, 2: AES192, 3: AES256 "),
                                                  _("895")))
              {
                case 1:
                  mutt_str_replace (&sctx->smime_crypt_alg, "aes128");
                  break;
                case 2:
                  mutt_str_replace (&sctx->smime_crypt_alg, "aes192");
                  break;
                case 3:
                  mutt_str_replace (&sctx->smime_crypt_alg, "aes256");
                  break;
              }
              break;

            case 4: /* (c)lear */
              FREE (&sctx->smime_crypt_alg);
              sctx->smime_crypt_alg_cleared = 1;
              /* fall through */
            case -1: /* Ctrl-G or Enter */
              choice = 0;
              break;
          }
        } while (choice == -1);
      }
      break;

      case 's': /* (s)ign */
        msg->security &= ~ENCRYPT;
        msg->security |= SIGN;
        break;

      case 'S': /* (s)ign in oppenc mode */
        msg->security |= SIGN;
        break;

      case 'a': /* sign (a)s */

        if ((key = smime_ask_for_key (_("Sign as: "), KEYFLAG_CANSIGN, 0)))
        {
          mutt_str_replace (&sctx->smime_sign_as, key->hash);
          smime_free_key (&key);

          msg->security |= SIGN;

          /* probably need a different passphrase */
          crypt_smime_void_passphrase ();
        }

        break;

      case 'b': /* (b)oth */
        msg->security |= (ENCRYPT | SIGN);
        break;

      case 'f': /* (f)orget it: kept for backward compatibility. */
      case 'c': /* (c)lear */
        msg->security &= ~(ENCRYPT | SIGN);
        break;

      case 'F': /* (f)orget it or (c)lear in oppenc mode */
      case 'C':
        msg->security &= ~SIGN;
        break;

      case 'O': /* oppenc mode on */
        msg->security |= OPPENCRYPT;
        crypt_opportunistic_encrypt (msg);
        break;

      case 'o': /* oppenc mode off */
        msg->security &= ~OPPENCRYPT;
        break;
    }
  }
}


#endif /* CRYPT_BACKEND_CLASSIC_SMIME */
