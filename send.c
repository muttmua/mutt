/*
 * Copyright (C) 1996-2002,2004,2010,2012-2013 Michael R. Elkins <me@mutt.org>
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
#include "rfc2047.h"
#include "keymap.h"
#include "mime.h"
#include "mailbox.h"
#include "copy.h"
#include "mutt_crypt.h"
#include "mutt_idna.h"
#include "url.h"
#include "rfc3676.h"
#include "attach.h"
#include "send.h"
#include "background.h"

#ifdef USE_AUTOCRYPT
#include "autocrypt.h"
#endif

#include <ctype.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>
#include <sys/types.h>
#include <utime.h>

#ifdef MIXMASTER
#include "remailer.h"
#endif


static void append_signature (FILE *f)
{
  FILE *tmpfp;
  pid_t thepid;

  if (Signature && (tmpfp = mutt_open_read (Signature, &thepid)))
  {
    if (option (OPTSIGDASHES))
      fputs ("\n-- \n", f);
    mutt_copy_stream (tmpfp, f);
    safe_fclose (&tmpfp);
    if (thepid != -1)
      mutt_wait_filter (thepid);
  }
}

/* compare two e-mail addresses and return 1 if they are equivalent */
static int mutt_addrcmp (ADDRESS *a, ADDRESS *b)
{
  if (!a || !b)
    return 0;
  if (!a->mailbox || !b->mailbox)
    return 0;
  if (ascii_strcasecmp (a->mailbox, b->mailbox))
    return 0;
  return 1;
}

/* search an e-mail address in a list */
static int mutt_addrsrc (ADDRESS *a, ADDRESS *lst)
{
  for (; lst; lst = lst->next)
  {
    if (mutt_addrcmp (a, lst))
      return (1);
  }
  return (0);
}

/* removes addresses from "b" which are contained in "a" */
ADDRESS *mutt_remove_xrefs (ADDRESS *a, ADDRESS *b)
{
  ADDRESS *top, *p, *prev = NULL;

  top = b;
  while (b)
  {
    for (p = a; p; p = p->next)
    {
      if (mutt_addrcmp (p, b))
	break;
    }
    if (p)
    {
      if (prev)
      {
	prev->next = b->next;
	b->next = NULL;
	rfc822_free_address (&b);
	b = prev;
      }
      else
      {
	top = top->next;
	b->next = NULL;
	rfc822_free_address (&b);
	b = top;
      }
    }
    else
    {
      prev = b;
      b = b->next;
    }
  }
  return top;
}

/* remove any address which matches the current user.  if `leave_only' is
 * nonzero, don't remove the user's address if it is the only one in the list
 */
static ADDRESS *remove_user (ADDRESS *a, int leave_only)
{
  ADDRESS *top = NULL, *last = NULL;

  while (a)
  {
    if (!mutt_addr_is_user (a))
    {
      if (top)
      {
        last->next = a;
        last = last->next;
      }
      else
        last = top = a;
      a = a->next;
      last->next = NULL;
    }
    else
    {
      ADDRESS *tmp = a;

      a = a->next;
      if (!leave_only || a || last)
      {
	tmp->next = NULL;
	rfc822_free_address (&tmp);
      }
      else
	last = top = tmp;
    }
  }
  return top;
}

static ADDRESS *find_mailing_lists (ADDRESS *t, ADDRESS *c)
{
  ADDRESS *top = NULL, *ptr = NULL;

  for (; t || c; t = c, c = NULL)
  {
    for (; t; t = t->next)
    {
      if (mutt_is_mail_list (t) && !t->group)
      {
	if (top)
	{
	  ptr->next = rfc822_cpy_adr_real (t);
	  ptr = ptr->next;
	}
	else
	  ptr = top = rfc822_cpy_adr_real (t);
      }
    }
  }
  return top;
}

int mutt_edit_address (ADDRESS **a, const char *field, int expand_aliases)
{
  char buf[HUGE_STRING];
  char *err = NULL;
  int idna_ok = 0;

  do
  {
    buf[0] = 0;
    mutt_addrlist_to_local (*a);
    rfc822_write_address (buf, sizeof (buf), *a, 0);
    if (mutt_get_field (field, buf, sizeof (buf), MUTT_ALIAS) != 0)
      return (-1);
    rfc822_free_address (a);
    *a = mutt_parse_adrlist (NULL, buf);
    if (expand_aliases)
      *a = mutt_expand_aliases (*a);
    if ((idna_ok = mutt_addrlist_to_intl (*a, &err)) != 0)
    {
      mutt_error (_("Error: '%s' is a bad IDN."), err);
      mutt_refresh ();
      mutt_sleep (2);
      FREE (&err);
    }
  }
  while (idna_ok != 0);
  return 0;
}

static int edit_envelope (ENVELOPE *en)
{
  char buf[HUGE_STRING];
  LIST *uh = UserHeader;

  if (mutt_edit_address (&en->to, _("To: "), 1) == -1)
    return (-1);
  if (option (OPTASKCC) && mutt_edit_address (&en->cc, _("Cc: "), 1) == -1)
    return (-1);
  if (option (OPTASKBCC) && mutt_edit_address (&en->bcc, _("Bcc: "), 1) == -1)
    return (-1);
  if (!en->to && !en->cc && !en->bcc)
  {
    mutt_error _("No recipients were specified.");
    return (-1);
  }

  if (en->subject)
  {
    if (option (OPTFASTREPLY))
      return (0);
    else
      strfcpy (buf, en->subject, sizeof (buf));
  }
  else
  {
    const char *p;

    buf[0] = 0;
    for (; uh; uh = uh->next)
    {
      if (ascii_strncasecmp ("subject:", uh->data, 8) == 0)
      {
	p = skip_email_wsp(uh->data + 8);
	strfcpy (buf, p, sizeof (buf));
      }
    }
  }

  if (mutt_get_field (_("Subject: "), buf, sizeof (buf), 0) != 0 ||
      (!buf[0] && query_quadoption (OPT_SUBJECT, _("No subject, abort?")) != MUTT_NO))
  {
    mutt_message _("No subject, aborting.");
    return (-1);
  }
  mutt_str_replace (&en->subject, buf);

  return 0;
}

static void process_user_recips (ENVELOPE *env)
{
  LIST *uh = UserHeader;

  for (; uh; uh = uh->next)
  {
    if (ascii_strncasecmp ("to:", uh->data, 3) == 0)
      env->to = rfc822_parse_adrlist (env->to, uh->data + 3);
    else if (ascii_strncasecmp ("cc:", uh->data, 3) == 0)
      env->cc = rfc822_parse_adrlist (env->cc, uh->data + 3);
    else if (ascii_strncasecmp ("bcc:", uh->data, 4) == 0)
      env->bcc = rfc822_parse_adrlist (env->bcc, uh->data + 4);
  }
}

static void process_user_header (ENVELOPE *env)
{
  LIST *uh = UserHeader;
  LIST *last = env->userhdrs;

  if (last)
    while (last->next)
      last = last->next;

  for (; uh; uh = uh->next)
  {
    if (ascii_strncasecmp ("from:", uh->data, 5) == 0)
    {
      /* User has specified a default From: address.  Remove default address */
      rfc822_free_address (&env->from);
      env->from = rfc822_parse_adrlist (env->from, uh->data + 5);
    }
    else if (ascii_strncasecmp ("reply-to:", uh->data, 9) == 0)
    {
      rfc822_free_address (&env->reply_to);
      env->reply_to = rfc822_parse_adrlist (env->reply_to, uh->data + 9);
    }
    else if (ascii_strncasecmp ("message-id:", uh->data, 11) == 0)
    {
      char *tmp = mutt_extract_message_id (uh->data + 11, NULL, 0);
      if (rfc822_valid_msgid (tmp) >= 0)
      {
	FREE(&env->message_id);
	env->message_id = tmp;
      }
      else
	FREE(&tmp);
    }
    else if (ascii_strncasecmp ("to:", uh->data, 3) != 0 &&
	     ascii_strncasecmp ("cc:", uh->data, 3) != 0 &&
	     ascii_strncasecmp ("bcc:", uh->data, 4) != 0 &&
	     ascii_strncasecmp ("subject:", uh->data, 8) != 0 &&
	     ascii_strncasecmp ("return-path:", uh->data, 12) != 0)
    {
      if (last)
      {
	last->next = mutt_new_list ();
	last = last->next;
      }
      else
	last = env->userhdrs = mutt_new_list ();
      last->data = safe_strdup (uh->data);
    }
  }
}

void mutt_forward_intro (CONTEXT *ctx, HEADER *cur, FILE *fp)
{
  char buffer[LONG_STRING];

  if (ForwardAttrIntro)
  {
    setlocale (LC_TIME, NONULL (AttributionLocale));
    mutt_make_string (buffer, sizeof (buffer), ForwardAttrIntro, ctx, cur);
    setlocale (LC_TIME, "");
    fputs (buffer, fp);
    fputs ("\n\n", fp);
  }
}

void mutt_forward_trailer (CONTEXT *ctx, HEADER *cur, FILE *fp)
{
  char buffer[LONG_STRING];

  if (ForwardAttrTrailer)
  {
    setlocale (LC_TIME, NONULL (AttributionLocale));
    mutt_make_string (buffer, sizeof (buffer), ForwardAttrTrailer, ctx, cur);
    setlocale (LC_TIME, "");
    fputc ('\n', fp);
    fputs (buffer, fp);
    fputc ('\n', fp);
  }
}


static int include_forward (CONTEXT *ctx, HEADER *cur, FILE *out)
{
  int chflags = CH_DECODE, cmflags = MUTT_CM_FORWARDING;

  mutt_parse_mime_message (ctx, cur);
  mutt_message_hook (ctx, cur, MUTT_MESSAGEHOOK);

  if (WithCrypto && (cur->security & ENCRYPT) && option (OPTFORWDECODE))
  {
    /* make sure we have the user's passphrase before proceeding... */
    crypt_valid_passphrase (cur->security);
  }

  mutt_forward_intro (ctx, cur, out);

  if (option (OPTFORWDECODE))
  {
    cmflags |= MUTT_CM_DECODE | MUTT_CM_CHARCONV;
    if (option (OPTWEED))
    {
      chflags |= CH_WEED | CH_REORDER;
      cmflags |= MUTT_CM_WEED;
    }
  }
  if (option (OPTFORWQUOTE))
    cmflags |= MUTT_CM_PREFIX;

  /* wrapping headers for forwarding is considered a display
   * rather than send action */
  chflags |= CH_DISPLAY;

  mutt_copy_message (out, ctx, cur, cmflags, chflags);
  mutt_forward_trailer (ctx, cur, out);
  return 0;
}

static int inline_forward_attachments (CONTEXT *ctx, HEADER *cur,
                                       BODY ***plast, int *forwardq)
{
  BODY **last = *plast, *body;
  MESSAGE *msg = NULL;
  ATTACH_CONTEXT *actx = NULL;
  int rc = 0, i;

  mutt_parse_mime_message (ctx, cur);
  mutt_message_hook (ctx, cur, MUTT_MESSAGEHOOK);

  if ((msg = mx_open_message (ctx, cur->msgno, 0)) == NULL)
    return -1;

  actx = safe_calloc (sizeof(ATTACH_CONTEXT), 1);
  actx->hdr = cur;
  actx->root_fp = msg->fp;

  mutt_generate_recvattach_list (actx, actx->hdr, actx->hdr->content,
                                 actx->root_fp, -1, 0, 0);

  for (i = 0; i < actx->idxlen; i++)
  {
    body = actx->idx[i]->content;
    if ((body->type != TYPEMULTIPART) &&
	(!mutt_can_decode (body) ||
         (option (OPTHONORDISP) && body->disposition == DISPATTACH)) &&
        !(body->type == TYPEAPPLICATION &&
          (!ascii_strcasecmp (body->subtype, "pgp-signature") ||
           !ascii_strcasecmp (body->subtype, "x-pkcs7-signature") ||
           !ascii_strcasecmp (body->subtype, "pkcs7-signature"))))
    {
      /* Ask the quadoption only once */
      if (*forwardq == -1)
      {
        *forwardq = query_quadoption (OPT_FORWATTS,
        /* L10N:
           This is the prompt for $forward_attachments.
           When inline forwarding ($mime_forward answered "no"), this prompts
           whether to add non-decodable attachments from the original email.
           Text/plain parts and the like will already be included in the
           message contents, but other attachment, such as PDF files, will also
           be added as attachments to the new mail, if this is answered yes.
        */
                                      _("Forward attachments?"));
        if (*forwardq != MUTT_YES)
        {
          if (*forwardq == -1)
            rc = -1;
          goto cleanup;
        }
      }
      if (mutt_copy_body (actx->idx[i]->fp, last, body) == -1)
      {
        rc = -1;
	goto cleanup;
      }
      last = &((*last)->next);
    }
  }

cleanup:
  *plast = last;
  mx_close_message (ctx, &msg);
  mutt_free_attach_context (&actx);
  return rc;
}

static int mutt_inline_forward (CONTEXT *ctx, HEADER *msg, HEADER *cur, FILE *out)
{
  int i, forwardq = -1;
  BODY **last;

  if (cur)
    include_forward (ctx, cur, out);
  else
    for (i = 0; i < ctx->vcount; i++)
      if (ctx->hdrs[ctx->v2r[i]]->tagged)
        include_forward (ctx, ctx->hdrs[ctx->v2r[i]], out);

  if (option (OPTFORWDECODE) && (quadoption (OPT_FORWATTS) != MUTT_NO))
  {
    last = &msg->content;
    while (*last)
      last = &((*last)->next);

    if (cur)
    {
      if (inline_forward_attachments (ctx, cur, &last, &forwardq) != 0)
        return -1;
    }
    else
      for (i = 0; i < ctx->vcount; i++)
        if (ctx->hdrs[ctx->v2r[i]]->tagged)
        {
          if (inline_forward_attachments (ctx, ctx->hdrs[ctx->v2r[i]],
                                          &last, &forwardq) != 0)
            return -1;
          if (forwardq == MUTT_NO)
            break;
        }
  }

  return 0;
}


void mutt_make_attribution (CONTEXT *ctx, HEADER *cur, FILE *out)
{
  char buffer[LONG_STRING];
  if (Attribution)
  {
    setlocale (LC_TIME, NONULL (AttributionLocale));
    mutt_make_string (buffer, sizeof (buffer), Attribution, ctx, cur);
    setlocale (LC_TIME, "");
    fputs (buffer, out);
    fputc ('\n', out);
  }
}

void mutt_make_post_indent (CONTEXT *ctx, HEADER *cur, FILE *out)
{
  char buffer[STRING];
  if (PostIndentString)
  {
    mutt_make_string (buffer, sizeof (buffer), PostIndentString, ctx, cur);
    fputs (buffer, out);
    fputc ('\n', out);
  }
}

static int include_reply (CONTEXT *ctx, HEADER *cur, FILE *out)
{
  int cmflags = MUTT_CM_PREFIX | MUTT_CM_DECODE | MUTT_CM_CHARCONV | MUTT_CM_REPLYING;
  int chflags = CH_DECODE;

  if (WithCrypto && (cur->security & ENCRYPT))
  {
    /* make sure we have the user's passphrase before proceeding... */
    crypt_valid_passphrase (cur->security);
  }

  mutt_parse_mime_message (ctx, cur);
  mutt_message_hook (ctx, cur, MUTT_MESSAGEHOOK);

  mutt_make_attribution (ctx, cur, out);

  if (!option (OPTHEADER))
    cmflags |= MUTT_CM_NOHEADER;
  if (option (OPTWEED))
  {
    chflags |= CH_WEED | CH_REORDER;
    cmflags |= MUTT_CM_WEED;
  }

  mutt_copy_message (out, ctx, cur, cmflags, chflags);

  mutt_make_post_indent (ctx, cur, out);

  return 0;
}

static int default_to (ADDRESS **to, ENVELOPE *env, int flags, int hmfupto)
{
  char prompt[STRING];
  ADDRESS *default_addr = NULL;
  int default_prune = 0;

  if (flags && env->mail_followup_to && hmfupto == MUTT_YES)
  {
    rfc822_append (to, env->mail_followup_to, 1);
    return 0;
  }

  /* Exit now if we're setting up the default Cc list for list-reply
   * (only set if Mail-Followup-To is present and honoured).
   */
  if (flags & SENDLISTREPLY)
    return 0;

  if (!option(OPTREPLYSELF) && mutt_addr_is_user (env->from))
  {
    default_addr = env->to;
    default_prune = 1;
  }
  else
    default_addr = env->from;

  if (env->reply_to)
  {
    /* If the Reply-To: address is a mailing list, assume that it was
     * put there by the mailing list.
     */
    if (option (OPTIGNORELISTREPLYTO) &&
        mutt_is_mail_list (env->reply_to) &&
        (mutt_addrsrc (env->reply_to, env->to) ||
         mutt_addrsrc (env->reply_to, env->cc)))
    {
      rfc822_append (to, default_addr, default_prune);
      return 0;
    }

    /* Use the From header if our correspondent has a reply-to
     * header which is identical.
     *
     * Trac ticket 3909 mentioned a case where the reply-to display
     * name field had significance, so this is done more selectively
     * now.
     */
    if (default_addr == env->from &&
        mutt_addrcmp (env->from, env->reply_to) &&
        !env->from->next &&
        !env->reply_to->next &&
        (!env->reply_to->personal ||
         !mutt_strcasecmp (env->reply_to->personal, env->from->personal)))
    {
      rfc822_append (to, env->from, 0);
      return 0;
    }

    /* Aside from the above two exceptions, prompt via $reply_to quadoption.
     *
     * There are quite a few mailing lists which set the Reply-To:
     * header field to the list address, which makes it quite impossible
     * to send a message to only the sender of the message.  This
     * provides a way to do that.
     */

    /* L10N:
       Asks whether the user wishes respects the reply-to header when replying.
    */
    snprintf (prompt, sizeof (prompt), _("Reply to %s%s?"),
              env->reply_to->mailbox,
              env->reply_to->next?",...":"");
    switch (query_quadoption (OPT_REPLYTO, prompt))
    {
      case MUTT_YES:
        rfc822_append (to, env->reply_to, 0);
        break;

      case MUTT_NO:
        rfc822_append (to, default_addr, default_prune);
        break;

      default:
        return -1; /* abort */
    }
    return 0;
  }

  rfc822_append (to, default_addr, default_prune);
  return 0;
}

int mutt_fetch_recips (ENVELOPE *out, ENVELOPE *in, int flags)
{
  char prompt[STRING];
  ADDRESS *tmp;
  int hmfupto = -1;

  if ((flags & (SENDLISTREPLY|SENDGROUPREPLY|SENDGROUPCHATREPLY)) &&
      in->mail_followup_to)
  {
    snprintf (prompt, sizeof (prompt), _("Follow-up to %s%s?"),
	      in->mail_followup_to->mailbox,
	      in->mail_followup_to->next ? ",..." : "");

    if ((hmfupto = query_quadoption (OPT_MFUPTO, prompt)) == -1)
      return -1;
  }

  if (flags & SENDLISTREPLY)
  {
    tmp = find_mailing_lists (in->to, in->cc);
    rfc822_append (&out->to, tmp, 0);
    rfc822_free_address (&tmp);

    if (in->mail_followup_to && hmfupto == MUTT_YES &&
        default_to (&out->cc, in, flags & SENDLISTREPLY, hmfupto) == -1)
      return (-1); /* abort */
  }
  else if (flags & SENDTOSENDER)
    rfc822_append (&out->to, in->from, 0);
  else
  {
    if (default_to (&out->to, in, flags & (SENDGROUPREPLY|SENDGROUPCHATREPLY),
                    hmfupto) == -1)
      return (-1); /* abort */

    if ((flags & (SENDGROUPREPLY|SENDGROUPCHATREPLY)) &&
        (!in->mail_followup_to || hmfupto != MUTT_YES))
    {
      /* if (!mutt_addr_is_user(in->to)) */
      if (flags & SENDGROUPREPLY)
        rfc822_append (&out->cc, in->to, 1);
      else
        rfc822_append (&out->to, in->to, 1);
      rfc822_append (&out->cc, in->cc, 1);
    }
  }
  return 0;
}

static LIST *mutt_make_references(ENVELOPE *e)
{
  LIST *t = NULL, *l = NULL;

  if (e->references)
    l = mutt_copy_list (e->references);
  else
    l = mutt_copy_list (e->in_reply_to);

  if (e->message_id)
  {
    t = mutt_new_list();
    t->data = safe_strdup(e->message_id);
    t->next = l;
    l = t;
  }

  return l;
}

void mutt_fix_reply_recipients (ENVELOPE *env)
{
  if (! option (OPTMETOO))
  {
    /* the order is important here.  do the CC: first so that if the
     * the user is the only recipient, it ends up on the TO: field
     */
    env->cc = remove_user (env->cc, (env->to == NULL));
    env->to = remove_user (env->to, (env->cc == NULL) || option (OPTREPLYSELF));
  }

  /* the CC field can get cluttered, especially with lists */
  env->to = mutt_remove_duplicates (env->to);
  env->cc = mutt_remove_duplicates (env->cc);
  env->cc = mutt_remove_xrefs (env->to, env->cc);

  if (env->cc && !env->to)
  {
    env->to = env->cc;
    env->cc = NULL;
  }
}

void mutt_make_forward_subject (ENVELOPE *env, CONTEXT *ctx, HEADER *cur)
{
  char buffer[STRING];

  /* set the default subject for the message. */
  mutt_make_string (buffer, sizeof (buffer), NONULL(ForwFmt), ctx, cur);
  mutt_str_replace (&env->subject, buffer);
}

void mutt_make_misc_reply_headers (ENVELOPE *env, CONTEXT *ctx,
                                   HEADER *cur, ENVELOPE *curenv)
{
  /* This takes precedence over a subject that might have
   * been taken from a List-Post header.  Is that correct?
   */
  if (curenv->real_subj)
  {
    FREE (&env->subject);
    env->subject = safe_malloc (mutt_strlen (curenv->real_subj) + 5);
    sprintf (env->subject, "Re: %s", curenv->real_subj);	/* __SPRINTF_CHECKED__ */
  }
  else if (!env->subject)
    env->subject = safe_strdup ("Re:");
}

void mutt_add_to_reference_headers (ENVELOPE *env, ENVELOPE *curenv, LIST ***pp, LIST ***qq)
{
  LIST **p = NULL, **q = NULL;

  if (pp) p = *pp;
  if (qq) q = *qq;

  if (!p) p = &env->references;
  if (!q) q = &env->in_reply_to;

  while (*p) p = &(*p)->next;
  while (*q) q = &(*q)->next;

  *p = mutt_make_references (curenv);

  if (curenv->message_id)
  {
    *q = mutt_new_list();
    (*q)->data = safe_strdup (curenv->message_id);
  }

  if (pp) *pp = p;
  if (qq) *qq = q;

}

static void
mutt_make_reference_headers (ENVELOPE *curenv, ENVELOPE *env, CONTEXT *ctx)
{
  env->references = NULL;
  env->in_reply_to = NULL;

  if (!curenv)
  {
    HEADER *h;
    LIST **p = NULL, **q = NULL;
    int i;

    for (i = 0; i < ctx->vcount; i++)
    {
      h = ctx->hdrs[ctx->v2r[i]];
      if (h->tagged)
	mutt_add_to_reference_headers (env, h->env, &p, &q);
    }
  }
  else
    mutt_add_to_reference_headers (env, curenv, NULL, NULL);

  /* if there's more than entry in In-Reply-To (i.e. message has
     multiple parents), don't generate a References: header as it's
     discouraged by RfC2822, sect. 3.6.4 */
  if (ctx->tagged > 0 && env->in_reply_to && env->in_reply_to->next)
    mutt_free_list (&env->references);
}

static int
envelope_defaults (ENVELOPE *env, CONTEXT *ctx, HEADER *cur, int flags)
{
  ENVELOPE *curenv = NULL;
  int i = 0, tag = 0;

  if (!cur)
  {
    tag = 1;
    for (i = 0; i < ctx->vcount; i++)
      if (ctx->hdrs[ctx->v2r[i]]->tagged)
      {
	cur = ctx->hdrs[ctx->v2r[i]];
	curenv = cur->env;
	break;
      }

    if (!cur)
    {
      /* This could happen if the user tagged some messages and then did
       * a limit such that none of the tagged message are visible.
       */
      mutt_error _("No tagged messages are visible!");
      return (-1);
    }
  }
  else
    curenv = cur->env;

  if (flags & (SENDREPLY|SENDTOSENDER))
  {
    if (tag)
    {
      HEADER *h;

      for (i = 0; i < ctx->vcount; i++)
      {
	h = ctx->hdrs[ctx->v2r[i]];
	if (h->tagged && mutt_fetch_recips (env, h->env, flags) == -1)
	  return -1;
      }
    }
    else if (mutt_fetch_recips (env, curenv, flags) == -1)
      return -1;

    if ((flags & SENDLISTREPLY) && !env->to)
    {
      mutt_error _("No mailing lists found!");
      return (-1);
    }

    if (flags & SENDREPLY)
    {
      mutt_make_misc_reply_headers (env, ctx, cur, curenv);
      mutt_make_reference_headers (tag ? NULL : curenv, env, ctx);
    }
  }
  else if (flags & SENDFORWARD)
    mutt_make_forward_subject (env, ctx, cur);

  return (0);
}

static int
generate_body (FILE *tempfp,	/* stream for outgoing message */
	       HEADER *msg,	/* header for outgoing message */
	       int flags,	/* compose mode */
	       CONTEXT *ctx,	/* current mailbox */
	       HEADER *cur)	/* current message */
{
  int i;
  HEADER *h;
  BODY *tmp;

  if (flags & SENDREPLY)
  {
    if ((i = query_quadoption (OPT_INCLUDE, _("Include message in reply?"))) == -1)
      return (-1);

    if (i == MUTT_YES)
    {
      mutt_message _("Including quoted message...");
      if (!cur)
      {
	for (i = 0; i < ctx->vcount; i++)
	{
	  h = ctx->hdrs[ctx->v2r[i]];
	  if (h->tagged)
	  {
	    if (include_reply (ctx, h, tempfp) == -1)
	    {
	      mutt_error _("Could not include all requested messages!");
	      return (-1);
	    }
	    fputc ('\n', tempfp);
	  }
	}
      }
      else
	include_reply (ctx, cur, tempfp);

    }
  }
  else if (flags & SENDFORWARD)
  {
    if ((i = query_quadoption (OPT_MIMEFWD, _("Forward as attachment?"))) == MUTT_YES)
    {
      BODY *last = msg->content;

      mutt_message _("Preparing forwarded message...");

      while (last && last->next)
	last = last->next;

      if (cur)
      {
	tmp = mutt_make_message_attach (ctx, cur, 0);
        if (!tmp)
        {
          mutt_error _("Could not include all requested messages!");
          return -1;
        }
	if (last)
	  last->next = tmp;
	else
	  msg->content = tmp;
      }
      else
      {
	for (i = 0; i < ctx->vcount; i++)
	{
	  if (ctx->hdrs[ctx->v2r[i]]->tagged)
	  {
	    tmp = mutt_make_message_attach (ctx, ctx->hdrs[ctx->v2r[i]], 0);
            if (!tmp)
            {
              mutt_error _("Could not include all requested messages!");
              return -1;
            }
	    if (last)
	    {
	      last->next = tmp;
	      last = tmp;
	    }
	    else
	      last = msg->content = tmp;
	  }
	}
      }
    }
    else if (i != -1)
    {
      if (mutt_inline_forward (ctx, msg, cur, tempfp) != 0)
        return -1;
    }
    else if (i == -1)
      return -1;
  }
  /* if (WithCrypto && (flags & SENDKEY)) */
  else if ((WithCrypto & APPLICATION_PGP) && (flags & SENDKEY))
  {
    BODY *tmp;

    if ((WithCrypto & APPLICATION_PGP)
        && (tmp = crypt_pgp_make_key_attachment ()) == NULL)
      return -1;

    tmp->next = msg->content;
    msg->content = tmp;
  }

  mutt_clear_error ();

  return (0);
}

void mutt_set_followup_to (ENVELOPE *e)
{
  ADDRESS *t = NULL;
  ADDRESS *from;

  /*
   * Only generate the Mail-Followup-To if the user has requested it, and
   * it hasn't already been set
   */

  if (option (OPTFOLLOWUPTO) && !e->mail_followup_to)
  {
    if (mutt_is_list_cc (0, e->to, e->cc))
    {
      /*
       * this message goes to known mailing lists, so create a proper
       * mail-followup-to header
       */

      t = rfc822_append (&e->mail_followup_to, e->to, 0);
      rfc822_append (&t, e->cc, 1);
    }

    /* remove ourselves from the mail-followup-to header */
    e->mail_followup_to = remove_user (e->mail_followup_to, 0);

    /*
     * If we are not subscribed to any of the lists in question,
     * re-add ourselves to the mail-followup-to header.  The
     * mail-followup-to header generated is a no-op with group-reply,
     * but makes sure list-reply has the desired effect.
     */

    if (e->mail_followup_to && !mutt_is_list_recipient (0, e->to, e->cc))
    {
      if (e->reply_to)
	from = rfc822_cpy_adr (e->reply_to, 0);
      else if (e->from)
	from = rfc822_cpy_adr (e->from, 0);
      else
	from = mutt_default_from ();

      if (from)
      {
	/* Normally, this loop will not even be entered. */
	for (t = from; t && t->next; t = t->next)
	  ;

	t->next = e->mail_followup_to; 	/* t cannot be NULL at this point. */
	e->mail_followup_to = from;
      }
    }

    e->mail_followup_to = mutt_remove_duplicates (e->mail_followup_to);

  }
}

/* look through the recipients of the message we are replying to, and if
   we find an address that matches $alternates, we use that as the default
   from field */
static ADDRESS *set_reverse_name (SEND_CONTEXT *sctx, CONTEXT *ctx)
{
  ADDRESS *tmp = NULL;
  int i;

  if (sctx->cur)
    tmp = mutt_find_user_in_envelope (sctx->cur->env);
  else if (ctx && ctx->tagged)
  {
    for (i = 0; i < ctx->vcount; i++)
      if (ctx->hdrs[ctx->v2r[i]]->tagged)
        if ((tmp = mutt_find_user_in_envelope (ctx->hdrs[ctx->v2r[i]]->env)) != NULL)
          break;
  }

  if (tmp)
  {
    tmp = rfc822_cpy_adr_real (tmp);
    /* when $reverse_realname is not set, clear the personal name so that it
     * may be set vi a reply- or send-hook.
     */
    if (!option (OPTREVREAL))
    {
      FREE (&tmp->personal);
#ifdef EXACT_ADDRESS
      FREE (&tmp->val);
#endif
    }
  }
  return (tmp);
}

ADDRESS *mutt_default_from (void)
{
  ADDRESS *adr;
  const char *fqdn = mutt_fqdn(1);

  /*
   * Note: We let $from override $realname here.  Is this the right
   * thing to do?
   */

  if (From)
    adr = rfc822_cpy_adr_real (From);
  else if (option (OPTUSEDOMAIN))
  {
    adr = rfc822_new_address ();
    adr->mailbox = safe_malloc (mutt_strlen (Username) + mutt_strlen (fqdn) + 2);
    sprintf (adr->mailbox, "%s@%s", NONULL(Username), NONULL(fqdn));	/* __SPRINTF_CHECKED__ */
  }
  else
  {
    adr = rfc822_new_address ();
    adr->mailbox = safe_strdup (NONULL(Username));
  }

  return (adr);
}

static int generate_multipart_alternative (HEADER *msg, int flags)
{
  BODY *alternative;

  if (!SendMultipartAltFilter)
    return 0;

  /* In batch mode, only run if the quadoption is yes or ask-yes */
  if (flags & SENDBATCH)
  {
    if (!(quadoption (OPT_SENDMULTIPARTALT) & 0x1))
      return 0;
  }
  else
    switch (query_quadoption (OPT_SENDMULTIPARTALT,
                          /* L10N:
                             This is the query for the $send_multipart_alternative quadoption.
                             Answering yes generates an alternative content using
                             $send_multipart_alternative_filter
                          */
                          _("Generate multipart/alternative content?")))
    {
      case MUTT_NO:
	return 0;
      case MUTT_YES:
	break;
      case -1:
      default:
	return -1;
    }


  alternative = mutt_run_send_alternative_filter (msg);
  if (!alternative)
    return -1;

  msg->content = mutt_make_multipart_alternative (msg->content, alternative);

  return 0;
}

static int invoke_mta (SEND_CONTEXT *sctx)
{
  HEADER *msg = sctx->msg;
  BUFFER *tempfile = NULL;
  FILE *tempfp = NULL;
  int i = -1;
#ifdef USE_SMTP
  short old_write_bcc;
#endif

  /* Write out the message in MIME form. */
  tempfile = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tempfile);
  if ((tempfp = safe_fopen (mutt_b2s (tempfile), "w")) == NULL)
    goto cleanup;

#ifdef USE_SMTP
  old_write_bcc = option (OPTWRITEBCC);
  if (SmtpUrl)
    unset_option (OPTWRITEBCC);
#endif
#ifdef MIXMASTER
  mutt_write_rfc822_header (tempfp, msg->env, msg->content, sctx->date_header,
                            MUTT_WRITE_HEADER_NORMAL, msg->chain ? 1 : 0,
                            mutt_should_hide_protected_subject (msg));
#endif
#ifndef MIXMASTER
  mutt_write_rfc822_header (tempfp, msg->env, msg->content, sctx->date_header,
                            MUTT_WRITE_HEADER_NORMAL, 0,
                            mutt_should_hide_protected_subject (msg));
#endif
#ifdef USE_SMTP
  if (old_write_bcc)
    set_option (OPTWRITEBCC);
#endif

  fputc ('\n', tempfp); /* tie off the header. */

  if ((mutt_write_mime_body (msg->content, tempfp) == -1))
    goto cleanup;

  if (safe_fclose (&tempfp) != 0)
  {
    mutt_perror (mutt_b2s (tempfile));
    unlink (mutt_b2s (tempfile));
    goto cleanup;
  }

#ifdef MIXMASTER
  if (msg->chain)
    i = mix_send_message (msg->chain, mutt_b2s (tempfile));
  else
#endif

#if USE_SMTP
  if (SmtpUrl)
    i = mutt_smtp_send (msg->env->from, msg->env->to, msg->env->cc,
                        msg->env->bcc, mutt_b2s (tempfile),
                        (msg->content->encoding == ENC8BIT));
  else
#endif /* USE_SMTP */

  i = mutt_invoke_sendmail (msg->env->from, msg->env->to, msg->env->cc,
			    msg->env->bcc, mutt_b2s (tempfile),
                            (msg->content->encoding == ENC8BIT));

cleanup:
  if (tempfp)
  {
    safe_fclose (&tempfp);
    unlink (mutt_b2s (tempfile));
  }
  mutt_buffer_pool_release (&tempfile);
  return (i);
}

static int save_fcc_mailbox_part (BUFFER *fcc_mailbox, SEND_CONTEXT *sctx,
                                   int flags)
{
  int rc, choice;

  if (!option (OPTNOCURSES) && !(sctx->flags & SENDMAILX))
  {
    /* L10N:
       Message when saving fcc after sending.
       %s is the mailbox name.
    */
    mutt_message (_("Saving Fcc to %s"), mutt_b2s (fcc_mailbox));
  }

  mutt_buffer_expand_path (fcc_mailbox);

  if (!(mutt_buffer_len (fcc_mailbox) &&
        mutt_strcmp ("/dev/null", mutt_b2s (fcc_mailbox))))
    return 0;

  rc = mutt_write_fcc (mutt_b2s (fcc_mailbox), sctx, NULL, 0, NULL);
  if (rc && (flags & SENDBATCH))
  {
    /* L10N:
       Printed when a FCC in batch mode fails.  Batch mode will abort
       if $fcc_before_send is set.
       %s is the mailbox name.
    */
    mutt_error (_("Warning: Fcc to %s failed"), mutt_b2s (fcc_mailbox));
    return rc;
  }

  while (rc)
  {
    mutt_sleep (1);
    mutt_clear_error ();
    choice = mutt_multi_choice (
      /* L10N:
         Called when saving to $record or Fcc failed after sending.
         (r)etry tries the same mailbox again.
         alternate (m)ailbox prompts for a different mailbox to try.
         (s)kip aborts saving.
      */
      _("Fcc failed. (r)etry, alternate (m)ailbox, or (s)kip? "),
      /* L10N:
         These correspond to the "Fcc failed" multi-choice prompt
         (r)etry, alternate (m)ailbox, or (s)kip.
         Any similarity to famous leaders of the FSF is coincidental.
      */
      _("rms"));
    switch (choice)
    {
      case 2:   /* alternate (m)ailbox */
        /* L10N:
           This is the prompt to enter an "alternate (m)ailbox" when the
           initial Fcc fails.
        */
        rc = mutt_enter_mailbox (_("Fcc mailbox"), fcc_mailbox, 0);
        if ((rc == -1) || !mutt_buffer_len (fcc_mailbox))
        {
          rc = 0;
          break;
        }
        /* fall through */

      case 1:   /* (r)etry */
        rc = mutt_write_fcc (mutt_b2s (fcc_mailbox), sctx, NULL, 0, NULL);
        break;

      case -1:  /* abort */
      case 3:   /* (s)kip */
        rc = 0;
        break;
    }
  }

  return 0;
}

static int save_fcc (SEND_CONTEXT *sctx,
                     BODY *clear_content, char *pgpkeylist,
                     int flags)
{
  HEADER *msg;
  int rc = 0;
  BODY *tmpbody;
  BODY *save_content = NULL;
  BODY *save_sig = NULL;
  BODY *save_parts = NULL;
  int save_atts;

  if (!(mutt_buffer_len (sctx->fcc) &&
        mutt_strcmp ("/dev/null", mutt_b2s (sctx->fcc))))
    return rc;

  msg = sctx->msg;
  tmpbody = msg->content;

  /* Before sending, we don't allow message manipulation because it
   * will break message signatures.  This is especially complicated by
   * Protected Headers. */
  if (!option (OPTFCCBEFORESEND))
  {
    if (WithCrypto &&
        (msg->security & (ENCRYPT | SIGN | AUTOCRYPT))
        && option (OPTFCCCLEAR))
    {
      msg->content = clear_content;
      msg->security &= ~(ENCRYPT | SIGN | AUTOCRYPT);
      mutt_free_envelope (&msg->content->mime_headers);
      mutt_delete_parameter ("protected-headers", &msg->content->parameter);
    }

    /* check to see if the user wants copies of all attachments */
    save_atts = 1;
    if (msg->content->type == TYPEMULTIPART)
    {
      /* In batch mode, save attachments if the quadoption is yes or ask-yes */
      if (flags & SENDBATCH)
      {
        if (!(quadoption (OPT_FCCATTACH) & 0x1))
          save_atts = 0;
      }
      else if (query_quadoption (OPT_FCCATTACH, _("Save attachments in Fcc?")) != MUTT_YES)
        save_atts = 0;
    }
    if (!save_atts)
    {
      if (WithCrypto
          && (msg->security & (ENCRYPT | SIGN | AUTOCRYPT))
          && (mutt_strcmp (msg->content->subtype, "encrypted") == 0 ||
              mutt_strcmp (msg->content->subtype, "signed") == 0))
      {
        if ((clear_content->type == TYPEMULTIPART) &&
            !ascii_strcasecmp (clear_content->subtype, "mixed"))
        {
          if (!(msg->security & ENCRYPT) && (msg->security & SIGN))
          {
            /* save initial signature and attachments */
            save_sig = msg->content->parts->next;
            save_parts = clear_content->parts->next;
          }

          /* this means writing only the main part */
          msg->content = clear_content->parts;

          if (mutt_protect (sctx, pgpkeylist, 0) == -1)
          {
            /* we can't do much about it at this point, so
             * fallback to saving the whole thing to fcc
             */
            msg->content = tmpbody;
            save_sig = NULL;
            goto full_fcc;
          }

          save_content = msg->content;
        }
      }
      else if (!ascii_strcasecmp (msg->content->subtype, "mixed"))
        msg->content = msg->content->parts;
    }
  }

full_fcc:
  if (msg->content)
  {
    size_t delim_size;

    /* update received time so that when storing to a mbox-style folder
     * the From_ line contains the current time instead of when the
     * message was first postponed.
     */
    msg->received = time (NULL);

    /* Split fcc into comma separated mailboxes */
    delim_size = mutt_strlen (FccDelimiter);
    if (!delim_size)
      rc = save_fcc_mailbox_part (sctx->fcc, sctx, flags);
    else
    {
      BUFFER *fcc_mailbox;
      const char *mb_beg, *mb_end;

      fcc_mailbox = mutt_buffer_pool_get ();

      mb_beg = mutt_b2s (sctx->fcc);
      while (mb_beg && *mb_beg)
      {
        mb_end = strstr (mb_beg, FccDelimiter);
        if (mb_end)
        {
          mutt_buffer_substrcpy (fcc_mailbox, mb_beg, mb_end);
          mb_end += delim_size;
        }
        else
          mutt_buffer_strcpy (fcc_mailbox, mb_beg);

        if (mutt_buffer_len (fcc_mailbox))
          rc |= save_fcc_mailbox_part (fcc_mailbox, sctx, flags);

        mb_beg = mb_end;
      }

      mutt_buffer_pool_release (&fcc_mailbox);
    }
  }

  if (!option (OPTFCCBEFORESEND))
  {
    msg->content = tmpbody;

    if (WithCrypto && save_sig)
    {
      /* cleanup the second signature structures */
      if (save_content->parts)
      {
        mutt_free_body (&save_content->parts->next);
        save_content->parts = NULL;
      }
      mutt_free_body (&save_content);

      /* restore old signature and attachments */
      msg->content->parts->next = save_sig;
      msg->content->parts->parts->next = save_parts;
    }
    else if (WithCrypto && save_content)
    {
      /* destroy the new encrypted body. */
      mutt_free_body (&save_content);
    }
  }

  return rc;
}

/* rfc2047 encode the content-descriptions */
void mutt_encode_descriptions (BODY *b, short recurse)
{
  BODY *t;

  for (t = b; t; t = t->next)
  {
    if (t->description)
    {
      rfc2047_encode_string (&t->description);
    }
    if (recurse && t->parts)
      mutt_encode_descriptions (t->parts, recurse);
  }
}

/* rfc2047 decode them in case of an error */
static void decode_descriptions (BODY *b)
{
  BODY *t;

  for (t = b; t; t = t->next)
  {
    if (t->description)
    {
      rfc2047_decode (&t->description);
    }
    if (t->parts)
      decode_descriptions (t->parts);
  }
}

static void fix_end_of_file (const char *data)
{
  FILE *fp;
  int c;

  if ((fp = safe_fopen (data, "a+")) == NULL)
    return;
  fseek (fp,-1,SEEK_END);
  if ((c = fgetc(fp)) != '\n')
    fputc ('\n', fp);
  safe_fclose (&fp);
}

int mutt_resend_message (FILE *fp, CONTEXT *ctx, HEADER *cur)
{
  HEADER *msg = mutt_new_header ();

  if (mutt_prepare_template (fp, ctx, msg, cur, 1) < 0)
    return -1;

  if (WithCrypto)
  {
    /* mutt_prepare_template doesn't always flip on an application bit.
     * so fix that here */
    if (!(msg->security & (APPLICATION_SMIME | APPLICATION_PGP)))
    {
      if ((WithCrypto & APPLICATION_SMIME) && option (OPTSMIMEISDEFAULT))
        msg->security |= APPLICATION_SMIME;
      else if (WithCrypto & APPLICATION_PGP)
        msg->security |= APPLICATION_PGP;
      else
        msg->security |= APPLICATION_SMIME;
    }

    if (option (OPTCRYPTOPPORTUNISTICENCRYPT))
    {
      msg->security |= OPPENCRYPT;
      crypt_opportunistic_encrypt(msg);
    }
  }

  return mutt_send_message (SENDRESEND | SENDBACKGROUNDEDIT, msg, NULL, ctx, cur);
}

static int is_reply (HEADER *reply, HEADER *orig)
{
  return mutt_find_list (orig->env->references, reply->env->message_id) ||
    mutt_find_list (orig->env->in_reply_to, reply->env->message_id);
}

static int has_recips (ADDRESS *a)
{
  int c = 0;

  for ( ; a; a = a->next)
  {
    if (!a->mailbox || a->group)
      continue;
    c++;
  }
  return c;
}

static int has_attach_keyword (char *filename)
{
  int match = 0;
  char *buf = NULL;
  size_t blen = 0;
  FILE *fp;

  if ((fp = safe_fopen (filename, "r")) == NULL)
  {
    mutt_perror (filename);
    return 0;
  }

  while ((buf = mutt_read_line (buf, &blen, fp, NULL, 0)) != NULL)
  {
    if (!mutt_is_quote_line (buf, NULL) &&
        regexec (AbortNoattachRegexp.rx, buf, 0, NULL, 0) == 0)
    {
      match = 1;
      break;
    }
  }
  safe_fclose (&fp);
  FREE (&buf);

  return match;
}

static int postpone_message (SEND_CONTEXT *sctx)
{
  HEADER *msg;
  const char *fcc;
  int flags;
  char *pgpkeylist = NULL;
  char *encrypt_as = NULL;
  BODY *clear_content = NULL;

  if (!Postponed)
  {
    mutt_error _("Cannot postpone.  $postponed is unset");
    return -1;
  }

  msg = sctx->msg;
  fcc = mutt_b2s (sctx->fcc);
  flags = sctx->flags;

  if (msg->content->next)
    msg->content = mutt_make_multipart_mixed (msg->content);

  mutt_encode_descriptions (msg->content, 1);

  if (WithCrypto && option (OPTPOSTPONEENCRYPT) &&
      (msg->security & (ENCRYPT | AUTOCRYPT)))
  {
    if ((WithCrypto & APPLICATION_PGP) && (msg->security & APPLICATION_PGP))
      encrypt_as = PgpDefaultKey;
    else if ((WithCrypto & APPLICATION_SMIME) && (msg->security & APPLICATION_SMIME))
      encrypt_as = SmimeDefaultKey;
    if (!encrypt_as)
      encrypt_as = PostponeEncryptAs;

#ifdef USE_AUTOCRYPT
    if (msg->security & AUTOCRYPT)
    {
      if (mutt_autocrypt_set_sign_as_default_key (msg))
      {
        msg->content = mutt_remove_multipart_mixed (msg->content);
        decode_descriptions (msg->content);
        return -1;
      }
      encrypt_as = AutocryptDefaultKey;
    }
#endif

    if (encrypt_as)
    {
      pgpkeylist = safe_strdup (encrypt_as);
      clear_content = msg->content;
      if (mutt_protect (sctx, pgpkeylist, 1) == -1)
      {
        FREE (&pgpkeylist);
        msg->content = mutt_remove_multipart_mixed (msg->content);
        decode_descriptions (msg->content);
        return -1;
      }

      FREE (&pgpkeylist);
      mutt_encode_descriptions (msg->content, 0);
    }
  }

  /*
   * make sure the message is written to the right part of a maildir
   * postponed folder.
   */
  msg->read = 0; msg->old = 0;

  mutt_prepare_envelope (msg->env, 0);
  mutt_env_to_intl (msg->env, NULL, NULL);	/* Handle bad IDNAs the next time. */

  if (mutt_write_fcc (NONULL (Postponed), sctx,
                      (flags & SENDREPLY) ? sctx->cur_message_id : NULL,
                      1, fcc) < 0)
  {
    if (clear_content)
    {
      mutt_free_body (&msg->content);
      msg->content = clear_content;
    }

    /* protected headers cleanup: */
    mutt_free_envelope (&msg->content->mime_headers);
    mutt_delete_parameter ("protected-headers", &msg->content->parameter);
    FREE (&sctx->date_header);

    msg->content = mutt_remove_multipart_mixed (msg->content);
    decode_descriptions (msg->content);
    mutt_unprepare_envelope (msg->env);
    return -1;
  }

  mutt_update_num_postponed ();

  if (clear_content)
    mutt_free_body (&clear_content);

  return 0;
}

static SEND_SCOPE *scope_new (void)
{
  SEND_SCOPE *scope;

  scope = safe_calloc (1, sizeof(SEND_SCOPE));

  return scope;
}

static void scope_free (SEND_SCOPE **pscope)
{
  SEND_SCOPE *scope;

  if (!pscope || !*pscope)
    return;

  scope = *pscope;

  FREE (&scope->maildir);
  FREE (&scope->outbox);
  FREE (&scope->postponed);
  FREE (&scope->cur_folder);
  rfc822_free_address (&scope->env_from);
  rfc822_free_address (&scope->from);
  FREE (&scope->sendmail);
#if USE_SMTP
  FREE (&scope->smtp_url);
#endif
  FREE (&scope->pgp_sign_as);
  FREE (&scope->smime_sign_as);
  FREE (&scope->smime_crypt_alg);

  FREE (pscope);      /* __FREE_CHECKED__ */
}

static SEND_SCOPE *scope_save (void)
{
  SEND_SCOPE *scope;

  scope = scope_new ();

  memcpy (scope->options, Options, sizeof(scope->options));
  memcpy (scope->quadoptions, QuadOptions, sizeof(scope->quadoptions));

  scope->maildir = safe_strdup (Maildir);
  scope->outbox = safe_strdup (Outbox);
  scope->postponed = safe_strdup (Postponed);
  scope->cur_folder = safe_strdup (CurrentFolder);

  scope->env_from = rfc822_cpy_adr (EnvFrom, 0);
  scope->from = rfc822_cpy_adr (From, 0);

  scope->sendmail = safe_strdup (Sendmail);
#if USE_SMTP
  scope->smtp_url = safe_strdup (SmtpUrl);
#endif
  scope->pgp_sign_as = safe_strdup (PgpSignAs);
  scope->smime_sign_as = safe_strdup (SmimeSignAs);
  scope->smime_crypt_alg = safe_strdup (SmimeCryptAlg);

  return scope;
}

static void scope_restore (SEND_SCOPE *scope)
{
  if (!scope)
    return;

  memcpy (Options, scope->options, sizeof(scope->options));
  memcpy (QuadOptions, scope->quadoptions, sizeof(scope->quadoptions));

  mutt_str_replace (&Maildir, scope->maildir);
  mutt_str_replace (&Outbox, scope->outbox);
  mutt_str_replace (&Postponed, scope->postponed);
  mutt_str_replace (&CurrentFolder, scope->cur_folder);

  rfc822_free_address (&EnvFrom);
  EnvFrom = rfc822_cpy_adr (scope->env_from, 0);

  rfc822_free_address (&From);
  From = rfc822_cpy_adr (scope->from, 0);

  mutt_str_replace (&Sendmail, scope->sendmail);
#if USE_SMTP
  mutt_str_replace (&SmtpUrl, scope->smtp_url);
#endif
  mutt_str_replace (&PgpSignAs, scope->pgp_sign_as);
  mutt_str_replace (&SmimeSignAs, scope->smime_sign_as);
  mutt_str_replace (&SmimeCryptAlg, scope->smime_crypt_alg);
}

static SEND_CONTEXT *send_ctx_new (void)
{
  SEND_CONTEXT *sendctx;

  sendctx = safe_calloc (1, sizeof(SEND_CONTEXT));

  return sendctx;
}

static void send_ctx_free (SEND_CONTEXT **psctx)
{
  SEND_CONTEXT *sctx;

  if (!psctx || !*psctx)
    return;
  sctx = *psctx;

  if (!(sctx->flags & SENDNOFREEHEADER))
    mutt_free_header (&sctx->msg);
  mutt_buffer_free (&sctx->fcc);
  mutt_buffer_free (&sctx->tempfile);
  FREE (&sctx->date_header);

  FREE (&sctx->cur_message_id);
  FREE (&sctx->ctx_realpath);

  mutt_free_list (&sctx->tagged_message_ids);

  scope_free (&sctx->global_scope);
  scope_free (&sctx->local_scope);

  FREE (&sctx->pgp_sign_as);
  FREE (&sctx->smime_sign_as);
  FREE (&sctx->smime_crypt_alg);

  FREE (psctx);    /* __FREE_CHECKED__ */
}

/* Pre-initial edit message setup.
 *
 * Returns 0 if this part of the process finished normally
 *        -1 if an error occured or the process was aborted
 */
static int send_message_setup (SEND_CONTEXT *sctx, const char *tempfile,
                               CONTEXT *ctx)
{
  FILE *tempfp = NULL;
  int rv = -1, i;
  int killfrom = 0;
  BODY *pbody;
  char *ctype;
  BUFFER *tmpbuffer;

  /* Prompt only for the <mail> operation. */
  if ((sctx->flags & SENDCHECKPOSTPONED) &&
      quadoption (OPT_RECALL) != MUTT_NO &&
      mutt_num_postponed (1))
  {
    /* If the user is composing a new message, check to see if there
     * are any postponed messages first.
     */
    if ((i = query_quadoption (OPT_RECALL, _("Recall postponed message?"))) == -1)
      goto cleanup;

    if (i == MUTT_YES)
      sctx->flags |= SENDPOSTPONED;
  }

  /* Allocate the buffer due to the long lifetime, but
   * pre-resize it to ensure there are no NULL data field issues */
  sctx->fcc = mutt_buffer_new ();
  mutt_buffer_increase_size (sctx->fcc, LONG_STRING);

  /* Delay expansion of aliases until absolutely necessary--shouldn't
   * be necessary unless we are prompting the user or about to execute a
   * send-hook.
   */

  if (!sctx->msg)
  {
    sctx->msg = mutt_new_header ();

    if (sctx->flags & SENDPOSTPONED)
    {
      if (mutt_get_postponed (ctx, sctx) < 0)
	goto cleanup;
    }

    if (sctx->flags & (SENDPOSTPONED|SENDRESEND))
    {
      if ((tempfp = safe_fopen (sctx->msg->content->filename, "a+")) == NULL)
      {
	mutt_perror (sctx->msg->content->filename);
	goto cleanup;
      }
    }

    if (!sctx->msg->env)
      sctx->msg->env = mutt_new_envelope ();
  }

  /* Parse and use an eventual list-post header */
  if ((sctx->flags & SENDLISTREPLY)
      && sctx->cur && sctx->cur->env && sctx->cur->env->list_post)
  {
    /* Use any list-post header as a template */
    url_parse_mailto (sctx->msg->env, NULL, sctx->cur->env->list_post);
    /* We don't let them set the sender's address. */
    rfc822_free_address (&sctx->msg->env->from);
  }

  if (! (sctx->flags & (SENDKEY | SENDPOSTPONED | SENDRESEND)))
  {
    /* When SENDDRAFTFILE is set, the caller has already
     * created the "parent" body structure.
     */
    if (! (sctx->flags & SENDDRAFTFILE))
    {
      pbody = mutt_new_body ();
      pbody->next = sctx->msg->content; /* don't kill command-line attachments */
      sctx->msg->content = pbody;

      if (!(ctype = safe_strdup (ContentType)))
        ctype = safe_strdup ("text/plain");
      mutt_parse_content_type (ctype, sctx->msg->content);
      FREE (&ctype);
      sctx->msg->content->unlink = 1;
      sctx->msg->content->use_disp = 0;
      sctx->msg->content->disposition = DISPINLINE;

      if (!tempfile)
      {
        tmpbuffer = mutt_buffer_pool_get ();
        mutt_buffer_mktemp (tmpbuffer);
        tempfp = safe_fopen (mutt_b2s (tmpbuffer), "w+");
        sctx->msg->content->filename = safe_strdup (mutt_b2s (tmpbuffer));
        mutt_buffer_pool_release (&tmpbuffer);
      }
      else
      {
        tempfp = safe_fopen (tempfile, "a+");
        sctx->msg->content->filename = safe_strdup (tempfile);
      }
    }
    else
      tempfp = safe_fopen (sctx->msg->content->filename, "a+");

    if (!tempfp)
    {
      dprint(1,(debugfile, "newsend_message: can't create tempfile %s (errno=%d)\n", sctx->msg->content->filename, errno));
      mutt_perror (sctx->msg->content->filename);
      goto cleanup;
    }
  }

  /* this is handled here so that the user can match ~f in send-hook */
  if (option (OPTREVNAME) && ctx &&
      !(sctx->flags & (SENDPOSTPONED|SENDRESEND)) &&
      (sctx->flags & (SENDREPLY | SENDFORWARD | SENDTOSENDER)))
  {
    /* we shouldn't have to worry about freeing `sctx->msg->env->from' before
     * setting it here since this code will only execute when doing some
     * sort of reply.  the pointer will only be set when using the -H command
     * line option.
     *
     * We shouldn't have to worry about alias expansion here since we are
     * either replying to a real or postponed message, therefore no aliases
     * should exist since the user has not had the opportunity to add
     * addresses to the list.  We just have to ensure the postponed messages
     * have their aliases expanded.
     */

    sctx->msg->env->from = set_reverse_name (sctx, ctx);
  }

  if (! (sctx->flags & (SENDPOSTPONED|SENDRESEND)) &&
      ! ((sctx->flags & SENDDRAFTFILE) && option (OPTRESUMEDRAFTFILES)))
  {
    if ((sctx->flags & (SENDREPLY | SENDFORWARD | SENDTOSENDER)) && ctx &&
	envelope_defaults (sctx->msg->env, ctx, sctx->cur, sctx->flags) == -1)
      goto cleanup;

    if (option (OPTHDRS))
      process_user_recips (sctx->msg->env);

    /* Expand aliases and remove duplicates/crossrefs */
    mutt_expand_aliases_env (sctx->msg->env);

    if (sctx->flags & SENDREPLY)
      mutt_fix_reply_recipients (sctx->msg->env);

    if (! (sctx->flags & (SENDMAILX|SENDBATCH)) &&
	! (option (OPTAUTOEDIT) && option (OPTEDITHDRS)) &&
	! ((sctx->flags & SENDREPLY) && option (OPTFASTREPLY)))
    {
      if (edit_envelope (sctx->msg->env) == -1)
	goto cleanup;
    }

    /* the from address must be set here regardless of whether or not
     * $use_from is set so that the `~P' (from you) operator in send-hook
     * patterns will work.  if $use_from is unset, the from address is killed
     * after send-hooks are evaluated */

    if (!sctx->msg->env->from)
    {
      sctx->msg->env->from = mutt_default_from ();
      killfrom = 1;
    }

    if ((sctx->flags & SENDREPLY) && sctx->cur)
    {
      /* change setting based upon message we are replying to */
      mutt_message_hook (ctx, sctx->cur, MUTT_REPLYHOOK);

      /*
       * set the replied flag for the message we are generating so that the
       * user can use ~Q in a send-hook to know when reply-hook's are also
       * being used.
       */
      sctx->msg->replied = 1;
    }

    /* change settings based upon recipients */

    mutt_message_hook (NULL, sctx->msg, MUTT_SENDHOOK);

    /*
     * Unset the replied flag from the message we are composing since it is
     * no longer required.  This is done here because the FCC'd copy of
     * this message was erroneously get the 'R'eplied flag when stored in
     * a maildir-style mailbox.
     */
    sctx->msg->replied = 0;

    /* $use_from and/or $from might have changed in a send-hook */
    if (killfrom)
    {
      rfc822_free_address (&sctx->msg->env->from);
      if (option (OPTUSEFROM) && !(sctx->flags & (SENDPOSTPONED|SENDRESEND)))
	sctx->msg->env->from = mutt_default_from ();
      killfrom = 0;
    }

    if (option (OPTHDRS))
      process_user_header (sctx->msg->env);

    if (sctx->flags & SENDBATCH)
      mutt_copy_stream (stdin, tempfp);

    if (option (OPTSIGONTOP) && ! (sctx->flags & (SENDMAILX|SENDKEY|SENDBATCH))
	&& Editor && mutt_strcmp (Editor, "builtin") != 0)
      append_signature (tempfp);

    /* include replies/forwarded messages, unless we are given a template */
    if (!tempfile && (ctx || !(sctx->flags & (SENDREPLY|SENDFORWARD)))
	&& generate_body (tempfp, sctx->msg, sctx->flags, ctx, sctx->cur) == -1)
      goto cleanup;

    if (!option (OPTSIGONTOP) && ! (sctx->flags & (SENDMAILX|SENDKEY|SENDBATCH))
	&& Editor && mutt_strcmp (Editor, "builtin") != 0)
      append_signature (tempfp);
  }

  /* Only set format=flowed for new messages.  Postponed/resent/draftfiles
   * should respect the original email.
   *
   * This is set here so that send-hook can be used to turn the option on.
   */
  if (!(sctx->flags & (SENDKEY | SENDPOSTPONED | SENDRESEND | SENDDRAFTFILE)))
  {
    if (option (OPTTEXTFLOWED) &&
        sctx->msg->content->type == TYPETEXT &&
        !ascii_strcasecmp (sctx->msg->content->subtype, "plain"))
      mutt_set_parameter ("format", "flowed", &sctx->msg->content->parameter);
  }

  /*
   * This hook is even called for postponed messages, and can, e.g., be
   * used for setting the editor, the sendmail path, or the
   * envelope sender.
   */
  mutt_message_hook (NULL, sctx->msg, MUTT_SEND2HOOK);

  /* wait until now to set the real name portion of our return address so
     that $realname can be set in a send-hook */
  if (sctx->msg->env->from && !sctx->msg->env->from->personal
      && !(sctx->flags & (SENDRESEND|SENDPOSTPONED)))
  {
    sctx->msg->env->from->personal = safe_strdup (Realname);
#ifdef EXACT_ADDRESS
    FREE (&sctx->msg->env->from->val);
#endif
  }

  if (!((WithCrypto & APPLICATION_PGP) && (sctx->flags & SENDKEY)))
    safe_fclose (&tempfp);

  rv = 0;

cleanup:
  safe_fclose (&tempfp);

  return rv;
}

/* Initial pre-compose menu edit, and actions before the compose menu.
 *
 * Returns 0 if this part of the process finished normally
 *        -1 if an error occured or the process was aborted
 *         2 if the initial edit was backgrounded
 */
static int send_message_resume_first_edit (SEND_CONTEXT *sctx)
{
  int rv = -1;
  int killfrom = 0;

  if (sctx->flags & SENDMAILX)
  {
    if (mutt_builtin_editor (sctx) == -1)
      goto cleanup;
  }
  else if (! (sctx->flags & SENDBATCH))
  {
    struct stat st;

    /* Resume background editing */
    if (sctx->state)
    {
      if (sctx->state == SEND_STATE_FIRST_EDIT)
      {
        if (stat (sctx->msg->content->filename, &st) == 0)
        {
          if (sctx->mtime != st.st_mtime)
            fix_end_of_file (sctx->msg->content->filename);
        }
        else
          mutt_perror (sctx->msg->content->filename);
      }
      else if (sctx->state == SEND_STATE_FIRST_EDIT_HEADERS)
      {
        mutt_edit_headers (Editor, sctx, MUTT_EDIT_HEADERS_RESUME);
        mutt_env_to_intl (sctx->msg->env, NULL, NULL);
      }
      sctx->state = 0;
    }
    else
    {
      sctx->mtime = mutt_decrease_mtime (sctx->msg->content->filename, NULL);
      if (sctx->mtime == (time_t) -1)
      {
        mutt_perror (sctx->msg->content->filename);
        goto cleanup;
      }
      mutt_update_encoding (sctx->msg->content);

      /*
       * Select whether or not the user's editor should be called now.  We
       * don't want to do this when:
       * 1) we are sending a key/cert
       * 2) we are forwarding a message and the user doesn't want to edit it.
       *    This is controlled by the quadoption $forward_edit.  However, if
       *    both $edit_headers and $autoedit are set, we want to ignore the
       *    setting of $forward_edit because the user probably needs to add the
       *    recipients.
       */
      if (! (sctx->flags & SENDKEY) &&
          ((sctx->flags & SENDFORWARD) == 0 ||
           (option (OPTEDITHDRS) && option (OPTAUTOEDIT)) ||
           query_quadoption (OPT_FORWEDIT, _("Edit forwarded message?")) == MUTT_YES))
      {
        int background_edit;

        background_edit = (sctx->flags & SENDBACKGROUNDEDIT) &&
          option (OPTBACKGROUNDEDIT);

        /* If the this isn't a text message, look for a mailcap edit command */
        if (mutt_needs_mailcap (sctx->msg->content))
        {
          if (!mutt_edit_attachment (sctx->msg->content))
            goto cleanup;
        }
        else if (!Editor || mutt_strcmp ("builtin", Editor) == 0)
          mutt_builtin_editor (sctx);
        else if (option (OPTEDITHDRS))
        {
          mutt_env_to_local (sctx->msg->env);
          if (background_edit)
          {
            if (mutt_edit_headers (Editor, sctx, MUTT_EDIT_HEADERS_BACKGROUND) == 2)
            {
              sctx->state = SEND_STATE_FIRST_EDIT_HEADERS;
              return 2;
            }
          }
          else
            mutt_edit_headers (Editor, sctx, 0);

          mutt_env_to_intl (sctx->msg->env, NULL, NULL);
        }
        else
        {
          if (background_edit)
          {
            if (mutt_background_edit_file (sctx, Editor,
                                           sctx->msg->content->filename) == 2)
            {
              sctx->state = SEND_STATE_FIRST_EDIT;
              return 2;
            }
          }
          else
            mutt_edit_file (Editor, sctx->msg->content->filename);

          if (stat (sctx->msg->content->filename, &st) == 0)
          {
            if (sctx->mtime != st.st_mtime)
              fix_end_of_file (sctx->msg->content->filename);
          }
          else
            mutt_perror (sctx->msg->content->filename);
        }
      }
    }

    mutt_message_hook (NULL, sctx->msg, MUTT_SEND2HOOK);

    if (! (sctx->flags & (SENDPOSTPONED | SENDFORWARD | SENDKEY | SENDRESEND | SENDDRAFTFILE)))
    {
      if (stat (sctx->msg->content->filename, &st) == 0)
      {
	/* if the file was not modified, bail out now */
	if (sctx->mtime == st.st_mtime && !sctx->msg->content->next &&
	    query_quadoption (OPT_ABORT, _("Abort unmodified message?")) == MUTT_YES)
	{
	  mutt_message _("Aborted unmodified message.");
	  goto cleanup;
	}
      }
      else
	mutt_perror (sctx->msg->content->filename);
    }
  }

  /*
   * Set the message security unless:
   * 1) crypto support is not enabled (WithCrypto==0)
   * 2) pgp: header field was present during message editing with $edit_headers (sctx->msg->security != 0)
   * 3) we are resending a message
   * 4) we are recalling a postponed message (don't override the user's saved settings)
   * 5) we are in mailx mode
   * 6) we are in batch mode
   *
   * This is done after allowing the user to edit the message so that security
   * settings can be configured with send2-hook and $edit_headers.
   */
  if (WithCrypto && (sctx->msg->security == 0) && !(sctx->flags & (SENDBATCH | SENDMAILX | SENDPOSTPONED | SENDRESEND)))
  {
    if (
#ifdef USE_AUTOCRYPT
      option (OPTAUTOCRYPT) && option (OPTAUTOCRYPTREPLY)
#else
      0
#endif
      && sctx->has_cur && (sctx->cur_security & AUTOCRYPT))
    {
      sctx->msg->security |= (AUTOCRYPT | AUTOCRYPT_OVERRIDE | APPLICATION_PGP);
    }
    else
    {
      if (option (OPTCRYPTAUTOSIGN))
        sctx->msg->security |= SIGN;
      if (option (OPTCRYPTAUTOENCRYPT))
        sctx->msg->security |= ENCRYPT;
      if (option (OPTCRYPTREPLYENCRYPT) && sctx->has_cur && (sctx->cur_security & ENCRYPT))
        sctx->msg->security |= ENCRYPT;
      if (option (OPTCRYPTREPLYSIGN) && sctx->has_cur && (sctx->cur_security & SIGN))
        sctx->msg->security |= SIGN;
      if (option (OPTCRYPTREPLYSIGNENCRYPTED) && sctx->has_cur && (sctx->cur_security & ENCRYPT))
        sctx->msg->security |= SIGN;
      if ((WithCrypto & APPLICATION_PGP) &&
          ((sctx->msg->security & (ENCRYPT | SIGN)) || option (OPTCRYPTOPPORTUNISTICENCRYPT)))
      {
        if (option (OPTPGPAUTOINLINE))
          sctx->msg->security |= INLINE;
        if (option (OPTPGPREPLYINLINE) && sctx->has_cur && (sctx->cur_security & INLINE))
          sctx->msg->security |= INLINE;
      }
    }

    if (sctx->msg->security || option (OPTCRYPTOPPORTUNISTICENCRYPT))
    {
      /*
       * When replying / forwarding, use the original message's
       * crypto system.  According to the documentation,
       * smime_is_default should be disregarded here.
       *
       * Problem: At least with forwarding, this doesn't really
       * make much sense. Should we have an option to completely
       * disable individual mechanisms at run-time?
       */
      if (sctx->has_cur)
      {
	if ((WithCrypto & APPLICATION_PGP) && option (OPTCRYPTAUTOPGP)
	    && (sctx->cur_security & APPLICATION_PGP))
	  sctx->msg->security |= APPLICATION_PGP;
	else if ((WithCrypto & APPLICATION_SMIME) && option (OPTCRYPTAUTOSMIME)
                 && (sctx->cur_security & APPLICATION_SMIME))
	  sctx->msg->security |= APPLICATION_SMIME;
      }

      /*
       * No crypto mechanism selected? Use availability + smime_is_default
       * for the decision.
       */
      if (!(sctx->msg->security & (APPLICATION_SMIME | APPLICATION_PGP)))
      {
	if ((WithCrypto & APPLICATION_SMIME) && option (OPTCRYPTAUTOSMIME)
	    && option (OPTSMIMEISDEFAULT))
	  sctx->msg->security |= APPLICATION_SMIME;
	else if ((WithCrypto & APPLICATION_PGP) && option (OPTCRYPTAUTOPGP))
	  sctx->msg->security |= APPLICATION_PGP;
	else if ((WithCrypto & APPLICATION_SMIME) && option (OPTCRYPTAUTOSMIME))
	  sctx->msg->security |= APPLICATION_SMIME;
      }
    }

    /* opportunistic encrypt relies on SMIME or PGP already being selected */
    if (option (OPTCRYPTOPPORTUNISTICENCRYPT))
    {
      /* If something has already enabled encryption, e.g. OPTCRYPTAUTOENCRYPT
       * or OPTCRYPTREPLYENCRYPT, then don't enable opportunistic encrypt for
       * the message.
       */
      if (! (sctx->msg->security & (ENCRYPT|AUTOCRYPT)))
      {
        sctx->msg->security |= OPPENCRYPT;
        crypt_opportunistic_encrypt(sctx->msg);
      }
    }

    /* No permissible mechanisms found.  Don't sign or encrypt. */
    if (!(sctx->msg->security & (APPLICATION_SMIME|APPLICATION_PGP)))
      sctx->msg->security = 0;
  }

  /* Deal with the corner case where the crypto module backend is not available.
   * This can happen if configured without pgp/smime and with gpgme, but
   * $crypt_use_gpgme is unset.
   */
  if (sctx->msg->security &&
      !crypt_has_module_backend (sctx->msg->security))
  {
    mutt_error _("No crypto backend configured.  Disabling message security setting.");
    mutt_sleep (1);
    sctx->msg->security = 0;
  }

  /* specify a default fcc.  if we are in batchmode, only save a copy of
   * the message if the value of $copy is yes or ask-yes */

  if (!mutt_buffer_len (sctx->fcc) &&
      !(sctx->flags & (SENDPOSTPONEDFCC)) &&
      (!(sctx->flags & SENDBATCH) || (quadoption (OPT_COPY) & 0x1)))
  {
    /* set the default FCC */
    if (!sctx->msg->env->from)
    {
      sctx->msg->env->from = mutt_default_from ();
      killfrom = 1; /* no need to check $use_from because if the user specified
		       a from address it would have already been set by now */
    }
    mutt_select_fcc (sctx->fcc, sctx->msg);
    if (killfrom)
    {
      rfc822_free_address (&sctx->msg->env->from);
      killfrom = 0;
    }
  }


  mutt_rfc3676_space_stuff (sctx->msg);

  mutt_update_encoding (sctx->msg->content);

  rv = 0;

cleanup:
  return rv;
}

/* Compose menu and post-compose menu sending
 *
 * Returns 0 if the message was successfully sent
 *        -1 if the message was aborted or an error occurred
 *         1 if the message was postponed
 *         2 if the message editing was backgrounded
 */
static int send_message_resume_compose_menu (SEND_CONTEXT *sctx)
{
  int rv = -1, i, mta_rc = 0;
  int free_clear_content = 0;
  char *tag = NULL, *err = NULL;
  char *pgpkeylist = NULL;
  BODY *clear_content = NULL;

  if (! (sctx->flags & (SENDMAILX | SENDBATCH)))
  {
main_loop:

    mutt_buffer_pretty_multi_mailbox (sctx->fcc, FccDelimiter);
    i = mutt_compose_menu (sctx);
    if (i == -1)
    {
      /* abort */
      mutt_message _("Mail not sent.");
      goto cleanup;
    }
    else if (i == 1)
    {
      if (postpone_message (sctx) != 0)
        goto main_loop;
      mutt_message _("Message postponed.");
      rv = 1;
      goto cleanup;
    }
    else if (i == 2)
    {
      rv = 2;
      goto cleanup;
    }
  }

  if (!has_recips (sctx->msg->env->to) && !has_recips (sctx->msg->env->cc) &&
      !has_recips (sctx->msg->env->bcc))
  {
    if (! (sctx->flags & SENDBATCH))
    {
      mutt_error _("No recipients are specified!");
      goto main_loop;
    }
    else
    {
      puts _("No recipients were specified.");
      goto cleanup;
    }
  }

  if (mutt_env_to_intl (sctx->msg->env, &tag, &err))
  {
    mutt_error (_("Bad IDN in \"%s\": '%s'"), tag, err);
    FREE (&err);
    if (!(sctx->flags & SENDBATCH))
      goto main_loop;
    else
      goto cleanup;
  }

  if (!sctx->msg->env->subject && ! (sctx->flags & SENDBATCH) &&
      (i = query_quadoption (OPT_SUBJECT, _("No subject, abort sending?"))) != MUTT_NO)
  {
    /* if the abort is automatic, print an error message */
    if (quadoption (OPT_SUBJECT) == MUTT_YES)
      mutt_error _("No subject specified.");
    goto main_loop;
  }

  /* Scan for a mention of an attachment in the message body and
   * prompt if there is none. */
  if (!(sctx->flags & SENDBATCH) &&
      (quadoption (OPT_ABORTNOATTACH) != MUTT_NO) &&
      AbortNoattachRegexp.pattern &&
      !sctx->msg->content->next &&
      (sctx->msg->content->type == TYPETEXT) &&
      !ascii_strcasecmp (sctx->msg->content->subtype, "plain") &&
      has_attach_keyword (sctx->msg->content->filename))
  {
    if (query_quadoption (OPT_ABORTNOATTACH, _("No attachments, abort sending?")) != MUTT_NO)
    {
      if (quadoption (OPT_ABORTNOATTACH) == MUTT_YES)
        mutt_error _("Attachment referenced in message is missing");
      goto main_loop;
    }
  }

  if (generate_multipart_alternative (sctx->msg, sctx->flags))
  {
    if (!(sctx->flags & SENDBATCH))
      goto main_loop;
    else
      goto cleanup;
  }

  if (sctx->msg->content->next)
    sctx->msg->content = mutt_make_multipart_mixed (sctx->msg->content);

  /*
   * Ok, we need to do it this way instead of handling all fcc stuff in
   * one place in order to avoid going to main_loop with encoded "env"
   * in case of error.  Ugh.
   */

  mutt_encode_descriptions (sctx->msg->content, 1);

  /*
   * Make sure that clear_content and free_clear_content are
   * properly initialized -- we may visit this particular place in
   * the code multiple times, including after a failed call to
   * mutt_protect().
   */

  clear_content = NULL;
  free_clear_content = 0;

  if (WithCrypto)
  {
    if (sctx->msg->security & (ENCRYPT | SIGN | AUTOCRYPT))
    {
      /* save the decrypted attachments */
      clear_content = sctx->msg->content;

      if ((crypt_get_keys (sctx->msg, &pgpkeylist, 0) == -1) ||
          mutt_protect (sctx, pgpkeylist, 0) == -1)
      {
        sctx->msg->content = mutt_remove_multipart_mixed (sctx->msg->content);
        sctx->msg->content = mutt_remove_multipart_alternative (sctx->msg->content);

	FREE (&pgpkeylist);

        decode_descriptions (sctx->msg->content);
        goto main_loop;
      }
      mutt_encode_descriptions (sctx->msg->content, 0);
    }

    /*
     * at this point, sctx->msg->content is one of the following three things:
     * - multipart/signed.  In this case, clear_content is a child.
     * - multipart/encrypted.  In this case, clear_content exists
     *   independently
     * - application/pgp.  In this case, clear_content exists independently.
     * - something else.  In this case, it's the same as clear_content.
     */

    /* This is ugly -- lack of "reporting back" from mutt_protect(). */

    if (clear_content && (sctx->msg->content != clear_content)
        && (sctx->msg->content->parts != clear_content))
      free_clear_content = 1;
  }

  if (!option (OPTNOCURSES) && !(sctx->flags & SENDMAILX))
    mutt_message _("Sending message...");

  mutt_prepare_envelope (sctx->msg->env, 1);

  if (option (OPTFCCBEFORESEND))
  {
    if (save_fcc (sctx, clear_content, pgpkeylist, sctx->flags) &&
        (sctx->flags & SENDBATCH))
    {
      /* L10N:
         In batch mode with $fcc_before_send set, Mutt will abort if any of
         the Fcc's fails.
      */
      puts _("Fcc failed.  Aborting sending.");
      goto cleanup;
    }
  }

  if ((mta_rc = invoke_mta (sctx)) < 0)
  {
    if (!(sctx->flags & SENDBATCH))
    {
      if (!WithCrypto)
        ;
      else if ((sctx->msg->security & (ENCRYPT | AUTOCRYPT)) ||
               ((sctx->msg->security & SIGN)
                && sctx->msg->content->type == TYPEAPPLICATION))
      {
	mutt_free_body (&sctx->msg->content); /* destroy PGP data */
	sctx->msg->content = clear_content;	/* restore clear text. */
      }
      else if ((sctx->msg->security & SIGN) &&
               sctx->msg->content->type == TYPEMULTIPART &&
               !ascii_strcasecmp (sctx->msg->content->subtype, "signed"))
      {
	mutt_free_body (&sctx->msg->content->parts->next);	     /* destroy sig */
	sctx->msg->content = mutt_remove_multipart (sctx->msg->content);
      }

      FREE (&pgpkeylist);

      /* protected headers cleanup: */
      mutt_free_envelope (&sctx->msg->content->mime_headers);
      mutt_delete_parameter ("protected-headers", &sctx->msg->content->parameter);
      FREE (&sctx->date_header);

      sctx->msg->content = mutt_remove_multipart_mixed (sctx->msg->content);
      sctx->msg->content = mutt_remove_multipart_alternative (sctx->msg->content);
      decode_descriptions (sctx->msg->content);
      mutt_unprepare_envelope (sctx->msg->env);
      goto main_loop;
    }
    else
    {
      puts _("Could not send the message.");
      goto cleanup;
    }
  }

  if (!option (OPTFCCBEFORESEND))
    save_fcc (sctx, clear_content, pgpkeylist, sctx->flags);

  if (WithCrypto)
    FREE (&pgpkeylist);

  if (WithCrypto && free_clear_content)
    mutt_free_body (&clear_content);

  /* set 'replied' flag only if the user didn't change/remove
     In-Reply-To: and References: headers during edit */
  if ((sctx->flags & SENDREPLY) && sctx->ctx_realpath)
  {
    CONTEXT *ctx = Context;

    if (!option (OPTNOCURSES) && !(sctx->flags & SENDMAILX))
    {
      /* L10N:
         After sending a message, if the message was a reply Mutt will try
         to set "replied" flags on the original message(s).
         Background sending may cause the original mailbox to be reopened,
         so this message was added in case that takes some time.
      */
      mutt_message _("Setting reply flags.");
    }

    if (!sctx->is_backgrounded && ctx)
    {
      if (sctx->cur)
        mutt_set_flag (ctx, sctx->cur, MUTT_REPLIED, is_reply (sctx->cur, sctx->msg));
      else if (!(sctx->flags & SENDPOSTPONED) && ctx->tagged)
      {
        for (i = 0; i < ctx->vcount; i++)
          if (ctx->hdrs[ctx->v2r[i]]->tagged)
            mutt_set_flag (ctx, ctx->hdrs[ctx->v2r[i]], MUTT_REPLIED,
                           is_reply (ctx->hdrs[ctx->v2r[i]], sctx->msg));
      }
    }
    else
    {
      int close_context = 0;

      if (!ctx || mutt_strcmp (sctx->ctx_realpath, ctx->realpath))
      {
        ctx = mx_open_mailbox (sctx->ctx_realpath, MUTT_NOSORT | MUTT_QUIET, NULL);
        if (ctx)
        {
          close_context = 1;
          /* A few connection strings display despite MUTT_QUIET, so refresh. */
          mutt_message _("Setting reply flags.");
        }
      }
      if (ctx)
      {
        HEADER *cur;

	if (!ctx->id_hash)
	  ctx->id_hash = mutt_make_id_hash (ctx);

        if (sctx->has_cur)
        {
          cur = hash_find (ctx->id_hash, sctx->cur_message_id);
          if (cur)
            mutt_set_flag (ctx, cur, MUTT_REPLIED, is_reply (cur, sctx->msg));
        }
        else
        {
          LIST *entry = sctx->tagged_message_ids;

          while (entry)
          {
            cur = hash_find (ctx->id_hash, (char *)entry->data);
            if (cur)
              mutt_set_flag (ctx, cur, MUTT_REPLIED, is_reply (cur, sctx->msg));
            entry = entry->next;
          }
        }
      }
      if (close_context)
      {
        int close_rc;

        close_rc = mx_close_mailbox (ctx, NULL);
        if (close_rc > 0)
          close_rc = mx_close_mailbox (ctx, NULL);
        if (close_rc != 0)
          mx_fastclose_mailbox (ctx);
        FREE (&ctx);
      }
    }
  }

  if (!option (OPTNOCURSES) && !(sctx->flags & SENDMAILX))
  {
    mutt_message (mta_rc == 0 ? _("Mail sent.") : _("Sending in background."));
    mutt_sleep (0);
  }

  rv = 0;

cleanup:
  return rv;
}

/* backgroundable and resumable part of the send process.
 *
 * *psctx will be freed unless the message is backgrounded again.
 *
 * Note that in this function, and the functions it calls, we don't
 * use sctx->cur directly.  Instead sctx->has_cur and related fields.
 * in sctx are used.
 *
 * Returns 0 if the message was successfully sent
 *        -1 if the message was aborted or an error occurred
 *         1 if the message was postponed
 *         2 if the message editing was backgrounded
 */
int mutt_send_message_resume (SEND_CONTEXT **psctx)
{
  int rv;
  SEND_CONTEXT *sctx;

  if (!psctx || !*psctx)
    return -1;
  sctx = *psctx;

  if (sctx->local_scope)
  {
    sctx->global_scope = scope_save ();
    scope_restore (sctx->local_scope);
    scope_free (&sctx->local_scope);
  }

  if (sctx->state <= SEND_STATE_FIRST_EDIT_HEADERS)
  {
    rv = send_message_resume_first_edit (sctx);
    if (rv != 0)
      goto cleanup;
  }

  rv = send_message_resume_compose_menu (sctx);

cleanup:
  if (rv == 2)
    sctx->local_scope = scope_save ();
  if (sctx->global_scope)
  {
    scope_restore (sctx->global_scope);
    scope_free (&sctx->global_scope);
  }

  if (rv != 2)
    send_ctx_free (psctx);
  else
  {
    /* L10N:
       Message displayed when the user chooses to background
       editing from the landing page.
    */
    mutt_message _("Editing backgrounded.");
  }

  return rv;
}

/*
 * Returns 0 if the message was successfully sent
 *        -1 if the message was aborted or an error occurred
 *         1 if the message was postponed
 *         2 if the message editing was backgrounded
 */
int
mutt_send_message (int flags,            /* send mode */
                   HEADER *msg,          /* template to use for new message */
                   const char *tempfile, /* file specified by -i or -H */
                   CONTEXT *ctx,         /* current mailbox */
                   HEADER *cur)          /* current message */
{
  SEND_CONTEXT *sctx;
  int rv = -1, i;

  sctx = send_ctx_new ();
  sctx->flags = flags;
  sctx->msg = msg;
  if (ctx)
    sctx->ctx_realpath = safe_strdup (ctx->realpath);
  if (cur)
  {
    sctx->cur = cur;
    sctx->has_cur = 1;
    sctx->cur_message_id = safe_strdup (cur->env->message_id);
    sctx->cur_security = cur->security;
  }
  else if ((sctx->flags & SENDREPLY) && ctx && ctx->tagged)
  {
    for (i = 0; i < ctx->vcount; i++)
      if (ctx->hdrs[ctx->v2r[i]]->tagged)
      {
        sctx->tagged_message_ids =
          mutt_add_list (sctx->tagged_message_ids,
                         ctx->hdrs[ctx->v2r[i]]->env->message_id);
      }
  }

  if (send_message_setup (sctx, tempfile, ctx) < 0)
  {
    send_ctx_free (&sctx);
    return -1;
  }

  rv = mutt_send_message_resume (&sctx);
  if (rv == 2)
  {
    sctx->cur = NULL;
    sctx->is_backgrounded = 1;
  }

  return rv;
}

/* vim: set sw=2: */
