/*
 * Copyright (C) 1996-2002,2004,2007 Michael R. Elkins <me@mutt.org>, and others
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
#include "mailbox.h"
#include "mutt_crypt.h"

#ifdef USE_COMPRESSED
#include "compress.h"
#endif

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

typedef struct hook
{
  int type;		/* hook type */
  REGEXP rx;		/* regular expression */
  char *command;	/* filename, command or pattern to execute */
  pattern_t *pattern;	/* used for fcc,save,send-hook */
  struct hook *next;
} HOOK;

static HOOK *Hooks = NULL;
static HASH *IdxFmtHooks = NULL;

static int current_hook_type = 0;

int mutt_parse_hook (BUFFER *buf, BUFFER *s, union pointer_long_t udata, BUFFER *err)
{
  HOOK *ptr;
  BUFFER *command, *pattern;
  int rc = -1, not = 0, token_flags = 0;
  regex_t *rx = NULL;
  pattern_t *pat = NULL;
  long data = udata.l;

  command = mutt_buffer_pool_get ();
  pattern = mutt_buffer_pool_get ();

  if (*s->dptr == '!')
  {
    s->dptr++;
    SKIPWS (s->dptr);
    not = 1;
  }

  mutt_extract_token (pattern, s, 0);

  if (!MoreArgs (s))
  {
    strfcpy (err->data, _("too few arguments"), err->dsize);
    goto cleanup;
  }

  /* These hook types have a "command" parameter.
   * It's useful be able include spaces without having to quote it.
   *
   * Note: MUTT_TOKEN_ESC_VARS was briefly added to the flag for
   * backwards compatibilty, but after research and discussion
   * was removed.
   */
  if (data & (MUTT_FOLDERHOOK | MUTT_SENDHOOK | MUTT_SEND2HOOK |
              MUTT_ACCOUNTHOOK | MUTT_REPLYHOOK | MUTT_MESSAGEHOOK))
    token_flags = MUTT_TOKEN_SPACE;

  mutt_extract_token (command, s, token_flags);

  if (!mutt_buffer_len (command))
  {
    strfcpy (err->data, _("too few arguments"), err->dsize);
    goto cleanup;
  }

  if (MoreArgs (s))
  {
    strfcpy (err->data, _("too many arguments"), err->dsize);
    goto cleanup;
  }

  if (data & (MUTT_FOLDERHOOK | MUTT_MBOXHOOK))
  {
    BUFFER *tmp = NULL;

    /* Accidentally using the ^ mailbox shortcut in the .muttrc is a
     * common mistake */
    if ((*(pattern->data) == '^') && (!CurrentFolder))
    {
      strfcpy (err->data, _("current mailbox shortcut '^' is unset"), err->dsize);
      goto cleanup;
    }

    tmp = mutt_buffer_pool_get ();
    mutt_buffer_strcpy (tmp, mutt_b2s (pattern));
    /* expand_relative off because this is a regexp also */
    _mutt_buffer_expand_path (tmp, MUTT_EXPAND_PATH_RX);

    /* Check for other mailbox shortcuts that expand to the empty string.
     * This is likely a mistake too */
    if (!mutt_buffer_len (tmp) && mutt_buffer_len (pattern))
    {
      strfcpy (err->data, _("mailbox shortcut expanded to empty regexp"), err->dsize);
      mutt_buffer_pool_release (&tmp);
      goto cleanup;
    }

    mutt_buffer_strcpy (pattern, mutt_b2s (tmp));
    mutt_buffer_pool_release (&tmp);
  }
#ifdef USE_COMPRESSED
  else if (data & (MUTT_APPENDHOOK | MUTT_OPENHOOK | MUTT_CLOSEHOOK))
  {
    if (mutt_comp_valid_command (mutt_b2s (command)) == 0)
    {
      strfcpy (err->data, _("badly formatted command string"), err->dsize);
      goto cleanup;
    }
  }
#endif
  else if (DefaultHook && !(data & (MUTT_CHARSETHOOK | MUTT_ICONVHOOK | MUTT_ACCOUNTHOOK))
           && (!WithCrypto || !(data & MUTT_CRYPTHOOK))
    )
  {
    /* At this stage remain only message-hooks, reply-hooks, send-hooks,
     * send2-hooks, save-hooks, and fcc-hooks: All those allowing full
     * patterns. If given a simple regexp, we expand $default_hook.
     */
    mutt_check_simple (pattern, DefaultHook);
  }

  if (data & MUTT_MBOXHOOK)
  {
    mutt_buffer_expand_path (command);
  }
  else if (data & MUTT_SAVEHOOK)
  {
    /* Do not perform relative path expansion.  "\^" can be expanded later:
     *   mutt_default_save() => mutt_addr_hook() => mutt_make_string()
     * which will perform backslash expansion, converting "\^" to "^".
     * The saving code then calls mutt_buffer_expand_path() after prompting.
     */
    mutt_buffer_expand_path_norel (command);
  }
  else if (data & MUTT_FCCHOOK)
  {
    /* Do not perform relative path expansion  "\^" can be expanded later:
     *   mutt_select_fcc() => mutt_addr_hook() => mutt_make_string()
     * which will perform backslash expansion, converting "\^" to "^".
     * save_fcc_mailbox_part() then calls mutt_buffer_expand_path() on each part.
     */
    mutt_buffer_expand_multi_path_norel (command, FccDelimiter);
  }

  /* check to make sure that a matching hook doesn't already exist */
  for (ptr = Hooks; ptr; ptr = ptr->next)
  {
    if (ptr->type == data &&
	ptr->rx.not == not &&
	!mutt_strcmp (mutt_b2s (pattern), ptr->rx.pattern))
    {
      if (data & (MUTT_FOLDERHOOK | MUTT_SENDHOOK | MUTT_SEND2HOOK | MUTT_MESSAGEHOOK | MUTT_ACCOUNTHOOK | MUTT_REPLYHOOK | MUTT_CRYPTHOOK))
      {
	/* these hooks allow multiple commands with the same
	 * pattern, so if we've already seen this pattern/command pair, just
	 * ignore it instead of creating a duplicate */
	if (!mutt_strcmp (ptr->command, mutt_b2s (command)))
	{
          rc = 0;
          goto cleanup;
	}
      }
      else
      {
	/* other hooks only allow one command per pattern, so update the
	 * entry with the new command.  this currently does not change the
	 * order of execution of the hooks, which i think is desirable since
	 * a common action to perform is to change the default (.) entry
	 * based upon some other information. */
	FREE (&ptr->command);
	ptr->command = safe_strdup (mutt_b2s (command));
        rc = 0;
        goto cleanup;
      }
    }
    if (!ptr->next)
      break;
  }

  if (data & (MUTT_SENDHOOK | MUTT_SEND2HOOK | MUTT_SAVEHOOK | MUTT_FCCHOOK | MUTT_MESSAGEHOOK | MUTT_REPLYHOOK))
  {
    int comp_flags;

    if (data & (MUTT_SEND2HOOK))
      comp_flags = MUTT_SEND_MODE_SEARCH;
    else if (data & (MUTT_SENDHOOK | MUTT_FCCHOOK))
      comp_flags = 0;
    else
      comp_flags = MUTT_FULL_MSG;

    if ((pat = mutt_pattern_comp (pattern->data,
                                  comp_flags,
				  err)) == NULL)
      goto cleanup;
  }
  else
  {
    int rv;
    /* Hooks not allowing full patterns: Check syntax of regexp */
    rx = safe_malloc (sizeof (regex_t));
#ifdef MUTT_CRYPTHOOK
    if ((rv = REGCOMP (rx, mutt_b2s (pattern), ((data & (MUTT_CRYPTHOOK|MUTT_CHARSETHOOK|MUTT_ICONVHOOK)) ? REG_ICASE : 0))) != 0)
#else
      if ((rv = REGCOMP (rx, mutt_b2s (pattern), (data & (MUTT_CHARSETHOOK|MUTT_ICONVHOOK)) ? REG_ICASE : 0)) != 0)
#endif /* MUTT_CRYPTHOOK */
      {
        regerror (rv, rx, err->data, err->dsize);
        FREE (&rx);
        goto cleanup;
      }
  }

  if (ptr)
  {
    ptr->next = safe_calloc (1, sizeof (HOOK));
    ptr = ptr->next;
  }
  else
    Hooks = ptr = safe_calloc (1, sizeof (HOOK));
  ptr->type = data;
  ptr->command = safe_strdup (mutt_b2s (command));
  ptr->pattern = pat;
  ptr->rx.pattern = safe_strdup (mutt_b2s (pattern));
  ptr->rx.rx = rx;
  ptr->rx.not = not;

  rc = 0;

cleanup:
  mutt_buffer_pool_release (&command);
  mutt_buffer_pool_release (&pattern);
  return rc;
}

static void delete_hook (HOOK *h)
{
  FREE (&h->command);
  FREE (&h->rx.pattern);
  if (h->rx.rx)
  {
    regfree (h->rx.rx);
  }
  mutt_pattern_free (&h->pattern);
  FREE (&h);
}

/* Deletes all hooks of type ``type'', or all defined hooks if ``type'' is 0 */
static void delete_hooks (int type)
{
  HOOK *h;
  HOOK *prev;

  while (h = Hooks, h && (type == 0 || type == h->type))
  {
    Hooks = h->next;
    delete_hook (h);
  }

  prev = h; /* Unused assignment to avoid compiler warnings */

  while (h)
  {
    if (type == h->type)
    {
      prev->next = h->next;
      delete_hook (h);
    }
    else
      prev = h;
    h = prev->next;
  }
}

static void delete_idxfmt_hooklist (void *list)
{
  HOOK *h, *next;

  h = (HOOK *)list;
  while (h)
  {
    next = h->next;
    delete_hook (h);
    h = next;
  }
}

static void delete_idxfmt_hooks (void)
{
  hash_destroy (&IdxFmtHooks, delete_idxfmt_hooklist);
}

int mutt_parse_idxfmt_hook (BUFFER *buf, BUFFER *s, union pointer_long_t udata, BUFFER *err)
{
  HOOK *hooks, *ptr;
  BUFFER *name, *pattern, *fmtstring;
  int rc = -1, not = 0;
  pattern_t *pat = NULL;
  long data = udata.l;

  name = mutt_buffer_pool_get ();
  pattern = mutt_buffer_pool_get ();
  fmtstring = mutt_buffer_pool_get ();

  if (!IdxFmtHooks)
    IdxFmtHooks = hash_create (30, MUTT_HASH_STRDUP_KEYS);

  if (!MoreArgs (s))
  {
    strfcpy (err->data, _("not enough arguments"), err->dsize);
    goto out;
  }
  mutt_extract_token (name, s, 0);
  hooks = hash_find (IdxFmtHooks, mutt_b2s (name));

  if (*s->dptr == '!')
  {
    s->dptr++;
    SKIPWS (s->dptr);
    not = 1;
  }
  mutt_extract_token (pattern, s, 0);

  if (!MoreArgs (s))
  {
    strfcpy (err->data, _("too few arguments"), err->dsize);
    goto out;
  }
  mutt_extract_token (fmtstring, s, 0);

  if (MoreArgs (s))
  {
    strfcpy (err->data, _("too many arguments"), err->dsize);
    goto out;
  }

  if (DefaultHook)
    mutt_check_simple (pattern, DefaultHook);

  /* check to make sure that a matching hook doesn't already exist */
  for (ptr = hooks; ptr; ptr = ptr->next)
  {
    if ((ptr->rx.not == not) &&
        !mutt_strcmp (mutt_b2s (pattern), ptr->rx.pattern))
    {
      FREE (&ptr->command);
      ptr->command = safe_strdup (mutt_b2s (fmtstring));
      rc = 0;
      goto out;
    }
    if (!ptr->next)
      break;
  }

  /* MUTT_PATTERN_DYNAMIC sets so that date ranges are regenerated during
   * matching.  This of course is slower, but index-format-hook is commonly
   * used for date ranges, and they need to be evaluated relative to "now", not
   * the hook compilation time.
   */
  if ((pat = mutt_pattern_comp (pattern->data,
                                MUTT_FULL_MSG | MUTT_PATTERN_DYNAMIC,
                                err)) == NULL)
    goto out;

  if (ptr)
  {
    ptr->next = safe_calloc (1, sizeof (HOOK));
    ptr = ptr->next;
  }
  else
    ptr = safe_calloc (1, sizeof (HOOK));
  ptr->type = data;
  ptr->command = safe_strdup (mutt_b2s (fmtstring));
  ptr->pattern = pat;
  ptr->rx.pattern = safe_strdup (mutt_b2s (pattern));
  ptr->rx.rx = NULL;
  ptr->rx.not = not;

  if (!hooks)
    hash_insert (IdxFmtHooks, mutt_b2s (name), ptr);

  rc = 0;

out:
  mutt_buffer_pool_release (&name);
  mutt_buffer_pool_release (&pattern);
  mutt_buffer_pool_release (&fmtstring);

  return rc;
}

int mutt_parse_unhook (BUFFER *buf, BUFFER *s, union pointer_long_t udata, BUFFER *err)
{
  while (MoreArgs (s))
  {
    mutt_extract_token (buf, s, 0);
    if (mutt_strcmp ("*", buf->data) == 0)
    {
      if (current_hook_type)
      {
	snprintf (err->data, err->dsize, "%s",
		  _("unhook: Can't do unhook * from within a hook."));
	return -1;
      }
      delete_hooks (0);
      delete_idxfmt_hooks ();
    }
    else
    {
      int type = mutt_get_hook_type (buf->data);

      if (!type)
      {
	snprintf (err->data, err->dsize,
                  _("unhook: unknown hook type: %s"), buf->data);
	return (-1);
      }
      if (current_hook_type == type)
      {
	snprintf (err->data, err->dsize,
		  _("unhook: Can't delete a %s from within a %s."),
		  buf->data, buf->data);
	return -1;
      }
      if (type == MUTT_IDXFMTHOOK)
        delete_idxfmt_hooks ();
      else
        delete_hooks (type);
    }
  }
  return 0;
}

void mutt_folder_hook (const char *path)
{
  HOOK *tmp = Hooks;
  BUFFER err;

  current_hook_type = MUTT_FOLDERHOOK;

  mutt_buffer_init (&err);
  err.dsize = STRING;
  err.data = safe_malloc (err.dsize);
  for (; tmp; tmp = tmp->next)
  {
    if (!tmp->command)
      continue;

    if (tmp->type & MUTT_FOLDERHOOK)
    {
      if ((regexec (tmp->rx.rx, path, 0, NULL, 0) == 0) ^ tmp->rx.not)
      {
	if (mutt_parse_rc_line (tmp->command, &err) == -1)
	{
	  mutt_error ("%s", err.data);
	  mutt_sleep (1);	/* pause a moment to let the user see the error */
	  current_hook_type = 0;
	  FREE (&err.data);

	  return;
	}
      }
    }
  }
  FREE (&err.data);

  current_hook_type = 0;
}

char *mutt_find_hook (int type, const char *pat)
{
  HOOK *tmp = Hooks;

  for (; tmp; tmp = tmp->next)
    if (tmp->type & type)
    {
      if (regexec (tmp->rx.rx, pat, 0, NULL, 0) == 0)
	return (tmp->command);
    }
  return (NULL);
}

void mutt_message_hook (CONTEXT *ctx, HEADER *hdr, int type)
{
  BUFFER err;
  HOOK *hook;
  pattern_cache_t cache;

  current_hook_type = type;

  mutt_buffer_init (&err);
  err.dsize = STRING;
  err.data = safe_malloc (err.dsize);
  memset (&cache, 0, sizeof (cache));
  for (hook = Hooks; hook; hook = hook->next)
  {
    if (!hook->command)
      continue;

    if (hook->type & type)
      if ((mutt_pattern_exec (hook->pattern, 0, ctx, hdr, &cache) > 0) ^ hook->rx.not)
      {
	if (mutt_parse_rc_line (hook->command, &err) != 0)
	{
	  mutt_error ("%s", err.data);
	  mutt_sleep (1);
	  current_hook_type = 0;
	  FREE (&err.data);

	  return;
	}
        /* Executing arbitrary commands could affect the pattern results,
         * so the cache has to be wiped */
        memset (&cache, 0, sizeof (cache));
      }
  }
  FREE (&err.data);

  current_hook_type = 0;
}

static int
mutt_addr_hook (char *path, size_t pathlen, int type, CONTEXT *ctx, HEADER *hdr)
{
  HOOK *hook;
  pattern_cache_t cache;

  memset (&cache, 0, sizeof (cache));
  /* determine if a matching hook exists */
  for (hook = Hooks; hook; hook = hook->next)
  {
    if (!hook->command)
      continue;

    if (hook->type & type)
      if ((mutt_pattern_exec (hook->pattern, 0, ctx, hdr, &cache) > 0) ^ hook->rx.not)
      {
	mutt_make_string (path, pathlen, hook->command, ctx, hdr);
	return 0;
      }
  }

  return -1;
}

void mutt_default_save (char *path, size_t pathlen, HEADER *hdr)
{
  *path = 0;
  if (mutt_addr_hook (path, pathlen, MUTT_SAVEHOOK, Context, hdr) != 0)
  {
    BUFFER *tmp = NULL;
    ADDRESS *adr;
    ENVELOPE *env = hdr->env;
    int fromMe = mutt_addr_is_user (env->from);

    if (!fromMe && env->reply_to && env->reply_to->mailbox)
      adr = env->reply_to;
    else if (!fromMe && env->from && env->from->mailbox)
      adr = env->from;
    else if (env->to && env->to->mailbox)
      adr = env->to;
    else if (env->cc && env->cc->mailbox)
      adr = env->cc;
    else
      adr = NULL;
    if (adr)
    {
      tmp = mutt_buffer_pool_get ();
      mutt_safe_path (tmp, adr);
      snprintf (path, pathlen, "=%s", mutt_b2s (tmp));
      mutt_buffer_pool_release (&tmp);
    }
  }
}

void mutt_select_fcc (BUFFER *path, HEADER *hdr)
{
  ADDRESS *adr;
  BUFFER *buf = NULL;
  ENVELOPE *env = hdr->env;

  mutt_buffer_increase_size (path, _POSIX_PATH_MAX);

  if (mutt_addr_hook (path->data, path->dsize, MUTT_FCCHOOK, NULL, hdr) != 0)
  {
    if ((option (OPTSAVENAME) || option (OPTFORCENAME)) &&
	(env->to || env->cc || env->bcc))
    {
      adr = env->to ? env->to : (env->cc ? env->cc : env->bcc);
      buf = mutt_buffer_pool_get ();
      mutt_safe_path (buf, adr);
      mutt_buffer_concat_path (path, NONULL(Maildir), mutt_b2s (buf));
      mutt_buffer_pool_release (&buf);
      if (!option (OPTFORCENAME) && mx_access (mutt_b2s (path), W_OK) != 0)
	mutt_buffer_strcpy (path, NONULL (Outbox));
    }
    else
      mutt_buffer_strcpy (path, NONULL (Outbox));
  }
  else
    mutt_buffer_fix_dptr (path);

  mutt_buffer_pretty_multi_mailbox (path, FccDelimiter);
}

static char *_mutt_string_hook (const char *match, int hook)
{
  HOOK *tmp = Hooks;

  for (; tmp; tmp = tmp->next)
  {
    if ((tmp->type & hook) &&
        ((match && regexec (tmp->rx.rx, match, 0, NULL, 0) == 0) ^ tmp->rx.not))
      return (tmp->command);
  }
  return (NULL);
}

static LIST *_mutt_list_hook (const char *match, int hook)
{
  HOOK *tmp = Hooks;
  LIST *matches = NULL;

  for (; tmp; tmp = tmp->next)
  {
    if ((tmp->type & hook) &&
        ((match && regexec (tmp->rx.rx, match, 0, NULL, 0) == 0) ^ tmp->rx.not))
      matches = mutt_add_list (matches, tmp->command);
  }
  return (matches);
}

char *mutt_charset_hook (const char *chs)
{
  return _mutt_string_hook (chs, MUTT_CHARSETHOOK);
}

char *mutt_iconv_hook (const char *chs)
{
  return _mutt_string_hook (chs, MUTT_ICONVHOOK);
}

LIST *mutt_crypt_hook (ADDRESS *adr)
{
  return _mutt_list_hook (adr->mailbox, MUTT_CRYPTHOOK);
}

#ifdef USE_SOCKET
void mutt_account_hook (const char* url)
{
  /* parsing commands with URLs in an account hook can cause a recursive
   * call. We just skip processing if this occurs. Typically such commands
   * belong in a folder-hook -- perhaps we should warn the user. */
  static int inhook = 0;

  HOOK* hook;
  BUFFER err;

  if (inhook)
    return;

  mutt_buffer_init (&err);
  err.dsize = STRING;
  err.data = safe_malloc (err.dsize);

  for (hook = Hooks; hook; hook = hook->next)
  {
    if (! (hook->command && (hook->type & MUTT_ACCOUNTHOOK)))
      continue;

    if ((regexec (hook->rx.rx, url, 0, NULL, 0) == 0) ^ hook->rx.not)
    {
      inhook = 1;

      if (mutt_parse_rc_line (hook->command, &err) == -1)
      {
	mutt_error ("%s", err.data);
	FREE (&err.data);
	mutt_sleep (1);

        inhook = 0;
	return;
      }

      inhook = 0;
    }
  }

  FREE (&err.data);
}
#endif

const char *mutt_idxfmt_hook (const char *name, CONTEXT *ctx, HEADER *hdr)
{
  HOOK *hooklist, *hook;
  pattern_cache_t cache;
  const char *fmtstring = NULL;

  if (!IdxFmtHooks)
    return NULL;

  current_hook_type = MUTT_IDXFMTHOOK;
  hooklist = hash_find (IdxFmtHooks, name);
  memset (&cache, 0, sizeof (cache));

  for (hook = hooklist; hook; hook = hook->next)
  {
    if ((mutt_pattern_exec (hook->pattern, 0, ctx, hdr, &cache) > 0) ^ hook->rx.not)
    {
      fmtstring = hook->command;
      break;
    }
  }

  current_hook_type = 0;

  return fmtstring;
}
