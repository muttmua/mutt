/*
 * Copyright (C) 1996-1997 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 1999-2000,2002-2004,2006 Thomas Roessler <roessler@does-not-exist.org>
 * Copyright (C) 2001 Thomas Roessler <roessler@does-not-exist.org>
 *                     Oliver Ehli <elmy@acm.org>
 * Copyright (C) 2003 Werner Koch <wk@gnupg.org>
 * Copyright (C) 2004 g10code GmbH
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
#include "mime.h"
#include "copy.h"
#include "mutt_crypt.h"
#include "mutt_idna.h"

#ifdef USE_AUTOCRYPT
#include "autocrypt.h"
#endif

#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif


/* print the current time to avoid spoofing of the signature output */
void crypt_current_time(STATE *s, char *app_name)
{
  time_t t;
  char p[STRING], tmp[STRING];

  if (!WithCrypto)
    return;

  if (option (OPTCRYPTTIMESTAMP))
  {
    t = time(NULL);
    strftime (p, sizeof (p), _(" (current time: %c)"), localtime (&t));
  }
  else
    *p = '\0';

  snprintf (tmp, sizeof (tmp), _("[-- %s output follows%s --]\n"), NONULL(app_name), p);
  state_attach_puts (tmp, s);
}



void crypt_forget_passphrase (void)
{
  if ((WithCrypto & APPLICATION_PGP))
    crypt_pgp_void_passphrase ();

  if ((WithCrypto & APPLICATION_SMIME))
    crypt_smime_void_passphrase ();

  if (WithCrypto)
    mutt_message _("Passphrase(s) forgotten.");
}


#if defined(HAVE_SETRLIMIT) && (!defined(DEBUG))

static void disable_coredumps (void)
{
  struct rlimit rl = {0, 0};
  static short done = 0;

  if (!done)
  {
    setrlimit (RLIMIT_CORE, &rl);
    done = 1;
  }
}

#endif /* HAVE_SETRLIMIT */


int crypt_valid_passphrase(int flags)
{
  int ret = 0;

# if defined(HAVE_SETRLIMIT) &&(!defined(DEBUG))
  disable_coredumps ();
# endif

  if ((WithCrypto & APPLICATION_PGP) && (flags & APPLICATION_PGP))
    ret = crypt_pgp_valid_passphrase ();

  if ((WithCrypto & APPLICATION_SMIME) && (flags & APPLICATION_SMIME))
    ret = crypt_smime_valid_passphrase ();

  return ret;
}


/* In postpone mode, signing is automatically disabled. */
int mutt_protect (SEND_CONTEXT *sctx, char *keylist, int postpone)
{
  HEADER *msg;
  BODY *pbody = NULL, *tmp_pbody = NULL;
  BODY *tmp_smime_pbody = NULL;
  BODY *tmp_pgp_pbody = NULL;
  ENVELOPE *protected_headers = NULL;
  int security, sign, has_retainable_sig = 0;
  int i, rc = -1;
  char *orig_pgp_sign_as = NULL,
    *orig_smime_sign_as = NULL,
    *orig_smime_crypt_alg = NULL;

  if (!WithCrypto)
    return -1;

  msg = sctx->msg;

  security = msg->security;
  sign = security & (AUTOCRYPT | SIGN);
  if (postpone)
  {
    sign = 0;
    security &= ~SIGN;
  }

  if (!(security & (ENCRYPT | AUTOCRYPT)) && !sign)
    return 0;

  if (sign &&
      !(security & AUTOCRYPT) &&
      !crypt_valid_passphrase (security))
    return (-1);

  /* Override with sctx settings, if they are set.
   * This can happen with the compose send_menus and when resuming a
   * postponed message.
   */
  if (sctx->pgp_sign_as)
  {
    orig_pgp_sign_as = safe_strdup (PgpSignAs);
    mutt_str_replace (&PgpSignAs, sctx->pgp_sign_as);
  }
  if (sctx->smime_sign_as)
  {
    orig_smime_sign_as = safe_strdup (SmimeSignAs);
    mutt_str_replace (&SmimeSignAs, sctx->smime_sign_as);
  }
  if (sctx->smime_crypt_alg || sctx->smime_crypt_alg_cleared)
  {
    orig_smime_crypt_alg = safe_strdup (SmimeCryptAlg);
    mutt_str_replace (&SmimeCryptAlg, sctx->smime_crypt_alg);
  }

  if ((WithCrypto & APPLICATION_PGP) &&
      !(security & AUTOCRYPT) &&
      ((security & PGPINLINE) == PGPINLINE))
  {
    if ((msg->content->type != TYPETEXT) ||
        ascii_strcasecmp (msg->content->subtype, "plain"))
    {
      if ((i = query_quadoption (OPT_PGPMIMEAUTO,
              _("Inline PGP can't be used with attachments.  Revert to PGP/MIME?"))) != MUTT_YES)
      {
        mutt_error _("Mail not sent: inline PGP can't be used with attachments.");
        goto cleanup;
      }
    }
    else if (!mutt_strcasecmp ("flowed",
                               mutt_get_parameter ("format", msg->content->parameter)))
    {
      if ((i = query_quadoption (OPT_PGPMIMEAUTO,
              _("Inline PGP can't be used with format=flowed.  Revert to PGP/MIME?"))) != MUTT_YES)
      {
        mutt_error _("Mail not sent: inline PGP can't be used with format=flowed.");
        goto cleanup;
      }
    }
    else
    {
      /* they really want to send it inline... go for it */
      if (!isendwin ()) mutt_endwin _("Invoking PGP...");
      pbody = crypt_pgp_traditional_encryptsign (msg->content, security, keylist);
      if (pbody)
      {
        msg->content = pbody;
        rc = 0;
        goto cleanup;
      }

      /* otherwise inline won't work...ask for revert */
      if ((i = query_quadoption (OPT_PGPMIMEAUTO, _("Message can't be sent inline.  Revert to using PGP/MIME?"))) != MUTT_YES)
      {
        mutt_error _("Mail not sent.");
        goto cleanup;
      }
    }

    /* go ahead with PGP/MIME */
  }

  if (!isendwin ()) mutt_endwin (NULL);

  if ((WithCrypto & APPLICATION_SMIME))
    tmp_smime_pbody = msg->content;
  if ((WithCrypto & APPLICATION_PGP))
    tmp_pgp_pbody   = msg->content;

  if (option (OPTCRYPTUSEPKA) && sign)
  {
    /* Set sender (necessary for e.g. PKA).  */

    if ((WithCrypto & APPLICATION_SMIME)
        && (security & APPLICATION_SMIME))
      crypt_smime_set_sender (msg->env->from->mailbox);
    else if ((WithCrypto & APPLICATION_PGP)
             && (security & APPLICATION_PGP))
      crypt_pgp_set_sender (msg->env->from->mailbox);
  }

  if (option (OPTCRYPTPROTHDRSWRITE))
  {
    BUFFER *date = NULL;
    protected_headers = mutt_new_envelope ();

    protected_headers->subject = safe_strdup (msg->env->subject);

    /* Note that we set sctx->date_header too so the values match */
    date = mutt_buffer_pool_get ();
    mutt_make_date (date);
    mutt_str_replace (&sctx->date_header, mutt_b2s (date));
    protected_headers->date = safe_strdup (mutt_b2s (date));
    mutt_buffer_pool_release (&date);

    protected_headers->from = rfc822_cpy_adr (msg->env->from, 0);
    protected_headers->to = rfc822_cpy_adr (msg->env->to, 0);
    protected_headers->cc = rfc822_cpy_adr (msg->env->cc, 0);
    protected_headers->reply_to = rfc822_cpy_adr (msg->env->reply_to, 0);

    mutt_prepare_envelope (protected_headers, 0);
    mutt_env_to_intl (protected_headers, NULL, NULL);

    mutt_free_envelope (&msg->content->mime_headers);
    msg->content->mime_headers = protected_headers;
    mutt_set_parameter ("protected-headers", "v1", &msg->content->parameter);
  }

  /* A note about msg->content->mime_headers.  If postpone or send
   * fails, the mime_headers is cleared out before returning to the
   * compose menu.  So despite the "robustness" code above and in the
   * gen_gossip_list function below, mime_headers will not be set when
   * entering mutt_protect().
   *
   * This is important to note because the user could toggle
   * $crypt_protected_headers_write or $autocrypt off back in the
   * compose menu.  We don't want mutt_write_rfc822_header() to write
   * stale data from one option if the other is set.
   */
#ifdef USE_AUTOCRYPT
  if (option (OPTAUTOCRYPT) &&
      !postpone &&
      (security & AUTOCRYPT))
  {
    mutt_autocrypt_generate_gossip_list (msg);
  }
#endif

  if (sign)
  {
    if ((WithCrypto & APPLICATION_SMIME)
        && (security & APPLICATION_SMIME))
    {
      if (!(tmp_pbody = crypt_smime_sign_message (msg->content)))
	goto cleanup;
      pbody = tmp_smime_pbody = tmp_pbody;
    }

    if ((WithCrypto & APPLICATION_PGP)
        && (security & APPLICATION_PGP)
        && (!(security & (ENCRYPT | AUTOCRYPT)) || option (OPTPGPRETAINABLESIG)))
    {
      if (!(tmp_pbody = crypt_pgp_sign_message (msg->content)))
        goto cleanup;

      has_retainable_sig = 1;
      sign = 0;
      pbody = tmp_pgp_pbody = tmp_pbody;
    }

    if (WithCrypto
        && (security & APPLICATION_SMIME)
	&& (security & APPLICATION_PGP))
    {
      /* here comes the draft ;-) */
    }
  }


  if (security & (ENCRYPT | AUTOCRYPT))
  {
    if ((WithCrypto & APPLICATION_SMIME)
        && (security & APPLICATION_SMIME))
    {
      if (!(tmp_pbody = crypt_smime_build_smime_entity (tmp_smime_pbody,
                                                        keylist)))
      {
	/* signed ? free it! */
	goto cleanup;
      }
      /* free tmp_body if messages was signed AND encrypted ... */
      if (tmp_smime_pbody != msg->content && tmp_smime_pbody != tmp_pbody)
      {
	/* detach and don't delete msg->content,
	   which tmp_smime_pbody->parts after signing. */
	tmp_smime_pbody->parts = tmp_smime_pbody->parts->next;
	msg->content->next = NULL;
	mutt_free_body (&tmp_smime_pbody);
      }
      pbody = tmp_pbody;
    }

    if ((WithCrypto & APPLICATION_PGP)
        && (security & APPLICATION_PGP))
    {
      if (!(pbody = crypt_pgp_encrypt_message (msg, tmp_pgp_pbody, keylist,
                                               sign)))
      {

	/* did we perform a retainable signature? */
	if (has_retainable_sig)
	{
	  /* remove the outer multipart layer */
	  tmp_pgp_pbody = mutt_remove_multipart (tmp_pgp_pbody);
	  /* get rid of the signature */
	  mutt_free_body (&tmp_pgp_pbody->next);
	}

	goto cleanup;
      }

      /* destroy temporary signature envelope when doing retainable
       * signatures.

       */
      if (has_retainable_sig)
      {
	tmp_pgp_pbody = mutt_remove_multipart (tmp_pgp_pbody);
	mutt_free_body (&tmp_pgp_pbody->next);
      }
    }
  }

  if (pbody)
  {
    msg->content = pbody;
    rc = 0;
  }

cleanup:
  if (rc != 0)
  {
    mutt_free_envelope (&msg->content->mime_headers);
    mutt_delete_parameter ("protected-headers", &msg->content->parameter);
    FREE (&sctx->date_header);
  }

  if (sctx->pgp_sign_as)
    mutt_str_replace (&PgpSignAs, orig_pgp_sign_as);
  if (sctx->smime_sign_as)
    mutt_str_replace (&SmimeSignAs, orig_smime_sign_as);
  if (sctx->smime_crypt_alg || sctx->smime_crypt_alg_cleared)
    mutt_str_replace (&SmimeCryptAlg, orig_smime_crypt_alg);

  return rc;
}




int mutt_is_multipart_signed (BODY *b)
{
  char *p;

  if (!b || !(b->type == TYPEMULTIPART) ||
      !b->subtype || ascii_strcasecmp(b->subtype, "signed"))
    return 0;

  if (!(p = mutt_get_parameter("protocol", b->parameter)))
    return 0;

  if (!(ascii_strcasecmp (p, "multipart/mixed")))
    return SIGN;

  if ((WithCrypto & APPLICATION_PGP)
      && !(ascii_strcasecmp (p, "application/pgp-signature")))
    return PGPSIGN;

  if ((WithCrypto & APPLICATION_SMIME)
      && !(ascii_strcasecmp (p, "application/x-pkcs7-signature")))
    return SMIMESIGN;
  if ((WithCrypto & APPLICATION_SMIME)
      && !(ascii_strcasecmp (p, "application/pkcs7-signature")))
    return SMIMESIGN;

  return 0;
}


int mutt_is_multipart_encrypted (BODY *b)
{
  if ((WithCrypto & APPLICATION_PGP))
  {
    char *p;

    if (!b || b->type != TYPEMULTIPART ||
        !b->subtype || ascii_strcasecmp (b->subtype, "encrypted") ||
        !(p = mutt_get_parameter ("protocol", b->parameter)) ||
        ascii_strcasecmp (p, "application/pgp-encrypted"))
      return 0;

    return PGPENCRYPT;
  }

  return 0;
}


int mutt_is_valid_multipart_pgp_encrypted (BODY *b)
{
  if (! mutt_is_multipart_encrypted (b))
    return 0;

  b = b->parts;
  if (!b || b->type != TYPEAPPLICATION ||
      !b->subtype || ascii_strcasecmp (b->subtype, "pgp-encrypted"))
    return 0;

  b = b->next;
  if (!b || b->type != TYPEAPPLICATION ||
      !b->subtype || ascii_strcasecmp (b->subtype, "octet-stream"))
    return 0;

  return PGPENCRYPT;
}


/*
 * This checks for the malformed layout caused by MS Exchange in
 * some cases:
 *  <multipart/mixed>
 *     <text/plain>
 *     <application/pgp-encrypted> [BASE64-encoded]
 *     <application/octet-stream> [BASE64-encoded]
 * See ticket #3742
 */
int mutt_is_malformed_multipart_pgp_encrypted (BODY *b)
{
  if (!(WithCrypto & APPLICATION_PGP))
    return 0;

  if (!b || b->type != TYPEMULTIPART ||
      !b->subtype || ascii_strcasecmp (b->subtype, "mixed"))
    return 0;

  b = b->parts;
  if (!b || b->type != TYPETEXT ||
      !b->subtype || ascii_strcasecmp (b->subtype, "plain") ||
      b->length != 0)
    return 0;

  b = b->next;
  if (!b || b->type != TYPEAPPLICATION ||
      !b->subtype || ascii_strcasecmp (b->subtype, "pgp-encrypted"))
    return 0;

  b = b->next;
  if (!b || b->type != TYPEAPPLICATION ||
      !b->subtype || ascii_strcasecmp (b->subtype, "octet-stream"))
    return 0;

  b = b->next;
  if (b)
    return 0;

  return PGPENCRYPT;
}


int mutt_is_application_pgp (BODY *m)
{
  int t = 0;
  char *p;

  if (m->type == TYPEAPPLICATION)
  {
    if (!ascii_strcasecmp (m->subtype, "pgp") || !ascii_strcasecmp (m->subtype, "x-pgp-message"))
    {
      if ((p = mutt_get_parameter ("x-action", m->parameter))
	  && (!ascii_strcasecmp (p, "sign") || !ascii_strcasecmp (p, "signclear")))
	t |= PGPSIGN;

      if ((p = mutt_get_parameter ("format", m->parameter)) &&
	  !ascii_strcasecmp (p, "keys-only"))
	t |= PGPKEY;

      if (!t) t |= PGPENCRYPT;  /* not necessarily correct, but... */
    }

    if (!ascii_strcasecmp (m->subtype, "pgp-signed"))
      t |= PGPSIGN;

    if (!ascii_strcasecmp (m->subtype, "pgp-keys"))
      t |= PGPKEY;
  }
  else if (m->type == TYPETEXT && ascii_strcasecmp ("plain", m->subtype) == 0)
  {
    if (((p = mutt_get_parameter ("x-mutt-action", m->parameter))
	 || (p = mutt_get_parameter ("x-action", m->parameter))
	 || (p = mutt_get_parameter ("action", m->parameter)))
        && !ascii_strncasecmp ("pgp-sign", p, 8))
      t |= PGPSIGN;
    else if (p && !ascii_strncasecmp ("pgp-encrypt", p, 11))
      t |= PGPENCRYPT;
    else if (p && !ascii_strncasecmp ("pgp-keys", p, 7))
      t |= PGPKEY;
  }
  if (t)
    t |= PGPINLINE;

  return t;
}

int mutt_is_application_smime (BODY *m)
{
  char *t=NULL;
  int len, complain=0;

  if (!m)
    return 0;

  if ((m->type & TYPEAPPLICATION) && m->subtype)
  {
    /* S/MIME MIME types don't need x- anymore, see RFC2311 */
    if (!ascii_strcasecmp (m->subtype, "x-pkcs7-mime") ||
	!ascii_strcasecmp (m->subtype, "pkcs7-mime"))
    {
      if ((t = mutt_get_parameter ("smime-type", m->parameter)))
      {
	if (!ascii_strcasecmp (t, "enveloped-data"))
	  return SMIMEENCRYPT;
	else if (!ascii_strcasecmp (t, "signed-data"))
	  return (SMIMESIGN|SMIMEOPAQUE);
	else return 0;
      }
      /* Netscape 4.7 uses
       * Content-Description: S/MIME Encrypted Message
       * instead of Content-Type parameter
       */
      if (!ascii_strcasecmp (m->description, "S/MIME Encrypted Message"))
	return SMIMEENCRYPT;
      complain = 1;
    }
    else if (ascii_strcasecmp (m->subtype, "octet-stream"))
      return 0;

    t = mutt_get_parameter ("name", m->parameter);

    if (!t) t = m->d_filename;
    if (!t) t = m->filename;
    if (!t)
    {
      if (complain)
	mutt_message (_("S/MIME messages with no hints on content are unsupported."));
      return 0;
    }

    /* no .p7c, .p10 support yet. */

    len = mutt_strlen (t) - 4;
    if (len > 0 && *(t+len) == '.')
    {
      len++;
      if (!ascii_strcasecmp ((t+len), "p7m"))
        /* Not sure if this is the correct thing to do, but
           it's required for compatibility with Outlook */
        return (SMIMESIGN|SMIMEOPAQUE);
      else if (!ascii_strcasecmp ((t+len), "p7s"))
	return (SMIMESIGN|SMIMEOPAQUE);
    }
  }

  return 0;
}






int crypt_query (BODY *m)
{
  int t = 0;

  if (!WithCrypto)
    return 0;

  if (!m)
    return 0;

  if (m->type == TYPEAPPLICATION)
  {
    if ((WithCrypto & APPLICATION_PGP))
      t |= mutt_is_application_pgp(m);

    if ((WithCrypto & APPLICATION_SMIME))
    {
      t |= mutt_is_application_smime(m);
      if (t && m->goodsig) t |= GOODSIGN;
      if (t && m->badsig) t |= BADSIGN;
    }
  }
  else if ((WithCrypto & APPLICATION_PGP) && m->type == TYPETEXT)
  {
    t |= mutt_is_application_pgp (m);
    if (t && m->goodsig)
      t |= GOODSIGN;
  }

  if (m->type == TYPEMULTIPART)
  {
    t |= mutt_is_multipart_encrypted(m);
    t |= mutt_is_multipart_signed (m);
    t |= mutt_is_malformed_multipart_pgp_encrypted (m);

    if (t && m->goodsig)
      t |= GOODSIGN;
#ifdef USE_AUTOCRYPT
    if (t && m->is_autocrypt)
      t |= AUTOCRYPT;
#endif
  }

  if (m->type == TYPEMULTIPART || m->type == TYPEMESSAGE)
  {
    BODY *p;
    int u, v, w;

    u = m->parts ? 0xffffffff : 0;	/* Bits set in all parts */
    w = 0;				/* Bits set in any part  */

    for (p = m->parts; p; p = p->next)
    {
      v  = crypt_query (p);
      u &= v; w |= v;
    }
    t |= u | (w & ~GOODSIGN);

    if ((w & GOODSIGN) && !(u & GOODSIGN))
      t |= PARTSIGN;
  }

  return t;
}




int crypt_write_signed(BODY *a, STATE *s, const char *tempfile)
{
  FILE *fp;
  int c;
  short hadcr;
  size_t bytes;

  if (!WithCrypto)
    return -1;

  if (!(fp = safe_fopen (tempfile, "w")))
  {
    mutt_perror (tempfile);
    return -1;
  }

  fseeko (s->fpin, a->hdr_offset, 0);
  bytes = a->length + a->offset - a->hdr_offset;
  hadcr = 0;
  while (bytes > 0)
  {
    if ((c = fgetc (s->fpin)) == EOF)
      break;

    bytes--;

    if  (c == '\r')
      hadcr = 1;
    else
    {
      if (c == '\n' && !hadcr)
	fputc ('\r', fp);

      hadcr = 0;
    }

    fputc (c, fp);

  }
  safe_fclose (&fp);

  return 0;
}



void convert_to_7bit (BODY *a)
{
  if (!WithCrypto)
    return;

  while (a)
  {
    if (a->type == TYPEMULTIPART)
    {
      if (a->encoding != ENC7BIT)
      {
        a->encoding = ENC7BIT;
	convert_to_7bit(a->parts);
      }
      else if ((WithCrypto & APPLICATION_PGP) && option (OPTPGPSTRICTENC))
	convert_to_7bit (a->parts);
    }
    else if (a->type == TYPEMESSAGE &&
	     ascii_strcasecmp(a->subtype, "delivery-status"))
    {
      if (a->encoding != ENC7BIT)
	mutt_message_to_7bit (a, NULL);
    }
    else if (a->encoding == ENC8BIT)
      a->encoding = ENCQUOTEDPRINTABLE;
    else if (a->encoding == ENCBINARY)
      a->encoding = ENCBASE64;
    else if (a->content && a->encoding != ENCBASE64 &&
	     (a->content->from || (a->content->space &&
				   option (OPTPGPSTRICTENC))))
      a->encoding = ENCQUOTEDPRINTABLE;
    a = a->next;
  }
}




void crypt_extract_keys_from_messages (HEADER * h)
{
  int i;
  BUFFER *tempfname = NULL;
  char *mbox;
  ADDRESS *tmp = NULL;
  FILE *fpout;

  if (!WithCrypto)
    return;

  tempfname = mutt_buffer_pool_get ();
  mutt_buffer_mktemp (tempfname);
  if (!(fpout = safe_fopen (mutt_b2s (tempfname), "w")))
  {
    mutt_perror (mutt_b2s (tempfname));
    goto cleanup;
  }

  if ((WithCrypto & APPLICATION_PGP))
    set_option (OPTDONTHANDLEPGPKEYS);

  if (!h)
  {
    for (i = 0; i < Context->vcount; i++)
    {
      if (Context->hdrs[Context->v2r[i]]->tagged)
      {
	mutt_parse_mime_message (Context, Context->hdrs[Context->v2r[i]]);
	if (Context->hdrs[Context->v2r[i]]->security & ENCRYPT &&
	    !crypt_valid_passphrase (Context->hdrs[Context->v2r[i]]->security))
	{
	  safe_fclose (&fpout);
	  break;
	}

	if ((WithCrypto & APPLICATION_PGP)
            && (Context->hdrs[Context->v2r[i]]->security & APPLICATION_PGP))
	{
	  mutt_copy_message (fpout, Context, Context->hdrs[Context->v2r[i]],
			     MUTT_CM_DECODE|MUTT_CM_CHARCONV, 0);
	  fflush(fpout);

	  mutt_endwin (_("Trying to extract PGP keys...\n"));
	  crypt_pgp_invoke_import (mutt_b2s (tempfname));
	}

	if ((WithCrypto & APPLICATION_SMIME)
            && (Context->hdrs[Context->v2r[i]]->security & APPLICATION_SMIME))
	{
	  if (Context->hdrs[Context->v2r[i]]->security & ENCRYPT)
	    mutt_copy_message (fpout, Context, Context->hdrs[Context->v2r[i]],
			       MUTT_CM_NOHEADER|MUTT_CM_DECODE_CRYPT
                               |MUTT_CM_DECODE_SMIME, 0);
	  else
	    mutt_copy_message (fpout, Context,
			       Context->hdrs[Context->v2r[i]], 0, 0);
	  fflush(fpout);

          if (Context->hdrs[Context->v2r[i]]->env->from)
	    tmp = mutt_expand_aliases (Context->hdrs[Context->v2r[i]]->env->from);
	  else if (Context->hdrs[Context->v2r[i]]->env->sender)
	    tmp = mutt_expand_aliases (Context->hdrs[Context->v2r[i]]->env->sender);
          mbox = tmp ? tmp->mailbox : NULL;
	  if (mbox)
	  {
	    mutt_endwin (_("Trying to extract S/MIME certificates...\n"));
	    crypt_smime_invoke_import (mutt_b2s (tempfname), mbox);
	    tmp = NULL;
	  }
	}

	rewind (fpout);
      }
    }
  }
  else
  {
    mutt_parse_mime_message (Context, h);
    if (!(h->security & ENCRYPT && !crypt_valid_passphrase (h->security)))
    {
      if ((WithCrypto & APPLICATION_PGP)
          && (h->security & APPLICATION_PGP))
      {
	mutt_copy_message (fpout, Context, h, MUTT_CM_DECODE|MUTT_CM_CHARCONV, 0);
	fflush(fpout);
	mutt_endwin (_("Trying to extract PGP keys...\n"));
	crypt_pgp_invoke_import (mutt_b2s (tempfname));
      }

      if ((WithCrypto & APPLICATION_SMIME)
          && (h->security & APPLICATION_SMIME))
      {
	if (h->security & ENCRYPT)
	  mutt_copy_message (fpout, Context, h,
                             MUTT_CM_NOHEADER | MUTT_CM_DECODE_CRYPT | MUTT_CM_DECODE_SMIME,
                             0);
	else
	  mutt_copy_message (fpout, Context, h, 0, 0);

	fflush(fpout);
	if (h->env->from) tmp = mutt_expand_aliases (h->env->from);
	else if (h->env->sender)  tmp = mutt_expand_aliases (h->env->sender);
	mbox = tmp ? tmp->mailbox : NULL;
	if (mbox) /* else ? */
	{
	  mutt_message (_("Trying to extract S/MIME certificates...\n"));
	  crypt_smime_invoke_import (mutt_b2s (tempfname), mbox);
	}
      }
    }
  }

  safe_fclose (&fpout);
  if (isendwin())
    mutt_any_key_to_continue (NULL);

  mutt_unlink (mutt_b2s (tempfname));

  if ((WithCrypto & APPLICATION_PGP))
    unset_option (OPTDONTHANDLEPGPKEYS);

cleanup:
  mutt_buffer_pool_release (&tempfname);
}



int crypt_get_keys (HEADER *msg, char **keylist, int oppenc_mode)
{
  ADDRESS *adrlist = NULL, *last = NULL;
  const char *fqdn = mutt_fqdn (1);
  char *self_encrypt = NULL;
  size_t keylist_size;

  /* Do a quick check to make sure that we can find all of the encryption
   * keys if the user has requested this service.
   */

  if (!WithCrypto)
    return 0;

  *keylist = NULL;

#ifdef USE_AUTOCRYPT
  if (!oppenc_mode && (msg->security & AUTOCRYPT))
  {
    if (mutt_autocrypt_ui_recommendation (msg, keylist) <= AUTOCRYPT_REC_NO)
      return (-1);
    return (0);
  }
#endif

  if ((WithCrypto & APPLICATION_PGP))
    set_option (OPTPGPCHECKTRUST);

  last = rfc822_append (&adrlist, msg->env->to, 0);
  last = rfc822_append (last ? &last : &adrlist, msg->env->cc, 0);
  rfc822_append (last ? &last : &adrlist, msg->env->bcc, 0);

  if (fqdn)
    rfc822_qualify (adrlist, fqdn);
  adrlist = mutt_remove_duplicates (adrlist);

  if (oppenc_mode || (msg->security & ENCRYPT))
  {
    if ((WithCrypto & APPLICATION_PGP)
        && (msg->security & APPLICATION_PGP))
    {
      if ((*keylist = crypt_pgp_findkeys (adrlist, oppenc_mode)) == NULL)
      {
        rfc822_free_address (&adrlist);
        return (-1);
      }
      unset_option (OPTPGPCHECKTRUST);
      if (option (OPTPGPSELFENCRYPT))
        self_encrypt = PgpDefaultKey;
    }
    if ((WithCrypto & APPLICATION_SMIME)
        && (msg->security & APPLICATION_SMIME))
    {
      if ((*keylist = crypt_smime_findkeys (adrlist, oppenc_mode)) == NULL)
      {
        rfc822_free_address (&adrlist);
        return (-1);
      }
      if (option (OPTSMIMESELFENCRYPT))
        self_encrypt = SmimeDefaultKey;
    }
  }

  if (!oppenc_mode && self_encrypt)
  {
    keylist_size = mutt_strlen (*keylist);
    safe_realloc (keylist, keylist_size + mutt_strlen (self_encrypt) + 2);
    sprintf (*keylist + keylist_size, " %s", self_encrypt);  /* __SPRINTF_CHECKED__ */
  }

  rfc822_free_address (&adrlist);

  return (0);
}


/*
 * Check if all recipients keys can be automatically determined.
 * Enable encryption if they can, otherwise disable encryption.
 */

void crypt_opportunistic_encrypt(HEADER *msg)
{
  char *pgpkeylist = NULL;

  if (!WithCrypto)
    return;

  if (! (option (OPTCRYPTOPPORTUNISTICENCRYPT) && (msg->security & OPPENCRYPT)) )
    return;

  crypt_get_keys (msg, &pgpkeylist, 1);
  if (pgpkeylist != NULL )
  {
    msg->security |= ENCRYPT;
    FREE (&pgpkeylist);
  }
  else
  {
    msg->security &= ~ENCRYPT;
  }
}



static void crypt_fetch_signatures (BODY ***signatures, BODY *a, int *n)
{
  if (!WithCrypto)
    return;

  for (; a; a = a->next)
  {
    if (a->type == TYPEMULTIPART)
      crypt_fetch_signatures (signatures, a->parts, n);
    else
    {
      if ((*n % 5) == 0)
	safe_realloc (signatures, (*n + 6) * sizeof (BODY **));

      (*signatures)[(*n)++] = a;
    }
  }
}

int mutt_should_hide_protected_subject (HEADER *h)
{
  if (option (OPTCRYPTPROTHDRSWRITE) &&
      (h->security & (ENCRYPT | AUTOCRYPT)) &&
      !(h->security & INLINE) &&
      ProtHdrSubject)
    return 1;

  return 0;
}

int mutt_protected_headers_handler (BODY *a, STATE *s)
{
  if (option (OPTCRYPTPROTHDRSREAD) && a->mime_headers)
  {
    if (a->mime_headers->subject)
    {
      if ((s->flags & MUTT_DISPLAY) && option (OPTWEED) &&
          mutt_matches_ignore ("subject", Ignore) &&
          !mutt_matches_ignore ("subject", UnIgnore))
        return 0;

      state_mark_protected_header (s);
      mutt_write_one_header (s->fpout, "Subject", a->mime_headers->subject,
                             s->prefix,
                             mutt_window_wrap_cols (MuttIndexWindow, Wrap),
                             (s->flags & MUTT_DISPLAY) ? CH_DISPLAY : 0);
      state_puts ("\n", s);
    }
  }

  return 0;
}

/*
 * This routine verifies a  "multipart/signed"  body.
 */

int mutt_signed_handler (BODY *a, STATE *s)
{
  BUFFER *tempfile = NULL;
  int signed_type;
  int inconsistent = 0;

  BODY *b = a;
  BODY **signatures = NULL;
  int sigcnt = 0;
  int i;
  short goodsig = 1;
  int rc = 0;

  if (!WithCrypto)
    return -1;

  a = a->parts;
  signed_type = mutt_is_multipart_signed (b);
  if (!signed_type)
  {
    /* A null protocol value is already checked for in mutt_body_handler() */
    state_printf (s, _("[-- Error: "
                       "Unknown multipart/signed protocol %s! --]\n\n"),
                  mutt_get_parameter ("protocol", b->parameter));
    return mutt_body_handler (a, s);
  }

  if (!(a && a->next))
    inconsistent = 1;
  else
  {
    switch (signed_type)
    {
      case SIGN:
        if (a->next->type != TYPEMULTIPART ||
            ascii_strcasecmp (a->next->subtype, "mixed"))
          inconsistent = 1;
        break;
      case PGPSIGN:
        if (a->next->type != TYPEAPPLICATION ||
            ascii_strcasecmp (a->next->subtype, "pgp-signature"))
          inconsistent = 1;
        break;
      case SMIMESIGN:
        if (a->next->type != TYPEAPPLICATION ||
            (ascii_strcasecmp (a->next->subtype, "x-pkcs7-signature") &&
             ascii_strcasecmp (a->next->subtype, "pkcs7-signature")))
          inconsistent = 1;
        break;
      default:
        inconsistent = 1;
    }
  }
  if (inconsistent)
  {
    state_attach_puts (_("[-- Error: "
                         "Missing or bad-format multipart/signed signature!"
                         " --]\n\n"),
                       s);
    return mutt_body_handler (a, s);
  }

  if (s->flags & MUTT_DISPLAY)
  {

    crypt_fetch_signatures (&signatures, a->next, &sigcnt);

    if (sigcnt)
    {
      tempfile = mutt_buffer_pool_get ();
      mutt_buffer_mktemp (tempfile);
      if (crypt_write_signed (a, s, mutt_b2s (tempfile)) == 0)
      {
	for (i = 0; i < sigcnt; i++)
	{
	  if ((WithCrypto & APPLICATION_PGP)
              && signatures[i]->type == TYPEAPPLICATION
	      && !ascii_strcasecmp (signatures[i]->subtype, "pgp-signature"))
	  {
	    if (crypt_pgp_verify_one (signatures[i], s, mutt_b2s (tempfile)) != 0)
	      goodsig = 0;

	    continue;
	  }

	  if ((WithCrypto & APPLICATION_SMIME)
              && signatures[i]->type == TYPEAPPLICATION
	      && (!ascii_strcasecmp(signatures[i]->subtype, "x-pkcs7-signature")
		  || !ascii_strcasecmp(signatures[i]->subtype, "pkcs7-signature")))
	  {
	    if (crypt_smime_verify_one (signatures[i], s, mutt_b2s (tempfile)) != 0)
	      goodsig = 0;

	    continue;
	  }

	  state_printf (s, _("[-- Warning: "
                             "We can't verify %s/%s signatures. --]\n\n"),
                        TYPE(signatures[i]), signatures[i]->subtype);
	}
      }

      mutt_unlink (mutt_b2s (tempfile));
      mutt_buffer_pool_release (&tempfile);

      b->goodsig = goodsig;
      b->badsig  = !goodsig;

      /* Now display the signed body */
      state_attach_puts (_("[-- The following data is signed --]\n\n"), s);

      mutt_protected_headers_handler (a, s);

      FREE (&signatures);
    }
    else
      state_attach_puts (_("[-- Warning: Can't find any signatures. --]\n\n"), s);
  }

  rc = mutt_body_handler (a, s);

  if (s->flags & MUTT_DISPLAY && sigcnt)
    state_attach_puts (_("\n[-- End of signed data --]\n"), s);

  return rc;
}


/* Obtain pointers to fingerprint or short or long key ID, if any.
 * See mutt_crypt.h for details.
 */
const char* crypt_get_fingerprint_or_id (char *p, const char **pphint,
                                         const char **ppl, const char **pps)
{
  const char *ps, *pl, *phint;
  char *pfcopy, *pf, *s1, *s2;
  char c;
  int isid;
  size_t hexdigits;

  /* User input may be partial name, fingerprint or short or long key ID,
   * independent of OPTPGPLONGIDS.
   * Fingerprint without spaces is 40 hex digits (SHA-1) or 32 hex digits (MD5).
   * Strip leading "0x" for key ID detection and prepare pl and ps to indicate
   * if an ID was found and to simplify logic in the key loop's inner
   * condition of the caller. */

  pf = mutt_skip_whitespace (p);
  if (!mutt_strncasecmp (pf, "0x", 2))
    pf += 2;

  /* Check if a fingerprint is given, must be hex digits only, blanks
   * separating groups of 4 hex digits are allowed. Also pre-check for ID. */
  isid = 2;             /* unknown */
  hexdigits = 0;
  s1 = pf;
  do
  {
    c = *(s1++);
    if (('0' <= c && c <= '9') || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f'))
    {
      ++hexdigits;
      if (isid == 2)
        isid = 1;       /* it is an ID so far */
    }
    else if (c)
    {
      isid = 0;         /* not an ID */
      if (c == ' ' && ((hexdigits % 4) == 0))
        ;               /* skip blank before or after 4 hex digits */
      else
        break;          /* any other character or position */
    }
  } while (c);

  /* If at end of input, check for correct fingerprint length and copy if. */
  pfcopy = (!c && ((hexdigits == 40) || (hexdigits == 32)) ? safe_strdup (pf) : NULL);

  if (pfcopy)
  {
    /* Use pfcopy to strip all spaces from fingerprint and as hint. */
    s1 = s2 = pfcopy;
    do
    {
      *(s1++) = *(s2 = mutt_skip_whitespace (s2));
    } while (*(s2++));

    phint = pfcopy;
    ps = pl = NULL;
  }
  else
  {
    phint = p;
    ps = pl = NULL;
    if (isid == 1)
    {
      if (mutt_strlen (pf) == 16)
        pl = pf;        /* long key ID */
      else if (mutt_strlen (pf) == 8)
        ps = pf;        /* short key ID */
    }
  }

  *pphint = phint;
  *ppl = pl;
  *pps = ps;
  return pfcopy;
}


/*
 * Used by pgp_findKeys and find_keys to check if a crypt-hook
 * value is a key id.
 */

short crypt_is_numerical_keyid (const char *s)
{
  /* or should we require the "0x"? */
  if (strncmp (s, "0x", 2) == 0)
    s += 2;
  if (strlen (s) % 8)
    return 0;
  while (*s)
    if (strchr ("0123456789ABCDEFabcdef", *s++) == NULL)
      return 0;

  return 1;
}
