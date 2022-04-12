/*
 * Copyright (C) 1996-2000,2002,2007,2010,2012 Michael R. Elkins <me@mutt.org>
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

#include "version.h"
#include "mutt.h"
#include "mutt_curses.h"
#include "mutt_idna.h"
#include "mutt_menu.h"
#include "mutt_crypt.h"
#include "rfc1524.h"
#include "mime.h"
#include "attach.h"
#include "mapping.h"
#include "mailbox.h"
#include "sort.h"
#include "charset.h"
#include "rfc3676.h"
#include "background.h"

#ifdef MIXMASTER
#include "remailer.h"
#endif

#ifdef USE_AUTOCRYPT
#include "autocrypt/autocrypt.h"
#endif

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

static const char* There_are_no_attachments = N_("There are no attachments.");

#define CHECK_COUNT if (actx->idxlen == 0) { mutt_error _(There_are_no_attachments); break; }

#define CURATTACH actx->idx[actx->v2r[menu->current]]


enum
{
  HDR_FROM  = 0,
  HDR_TO,
  HDR_CC,
  HDR_BCC,
  HDR_SUBJECT,
  HDR_REPLYTO,
  HDR_FCC,

#ifdef MIXMASTER
  HDR_MIX,
#endif

  HDR_CRYPT,
  HDR_CRYPTINFO,
#ifdef USE_AUTOCRYPT
  HDR_AUTOCRYPT,
#endif

  HDR_ATTACH_TITLE,     /* the "-- Attachments" line */
  HDR_ATTACH            /* where to start printing the attachments */
};

int HeaderPadding[HDR_ATTACH_TITLE] = {0};
int MaxHeaderWidth = 0;

#define HDR_XOFFSET MaxHeaderWidth
#define W (MuttIndexWindow->cols - MaxHeaderWidth)

static const char * const Prompts[] =
{
  /* L10N: Compose menu field.  May not want to translate. */
  N_("From: "),
  /* L10N: Compose menu field.  May not want to translate. */
  N_("To: "),
  /* L10N: Compose menu field.  May not want to translate. */
  N_("Cc: "),
  /* L10N: Compose menu field.  May not want to translate. */
  N_("Bcc: "),
  /* L10N: Compose menu field.  May not want to translate. */
  N_("Subject: "),
  /* L10N: Compose menu field.  May not want to translate. */
  N_("Reply-To: "),
  /* L10N: Compose menu field.  May not want to translate. */
  N_("Fcc: "),
#ifdef MIXMASTER
  /* L10N: "Mix" refers to the MixMaster chain for anonymous email */
  N_("Mix: "),
#endif
  /* L10N: Compose menu field.  Holds "Encrypt", "Sign" related information */
  N_("Security: "),
  /* L10N:
   * This string is used by the compose menu.
   * Since it is hidden by default, it does not increase the
   * indentation of other compose menu fields.  However, if possible,
   * it should not be longer than the other compose menu fields.
   *
   * Since it shares the row with "Encrypt with:", it should not be longer
   * than 15-20 character cells.
   */
  N_("Sign as: "),
#ifdef USE_AUTOCRYPT
  /* L10N:
     The compose menu autocrypt line
   */
  N_("Autocrypt: ")
#endif
};

static const struct mapping_t ComposeHelp[] = {
  { N_("Send"),    OP_COMPOSE_SEND_MESSAGE },
  { N_("Abort"),   OP_EXIT },
  /* L10N: compose menu help line entry */
  { N_("To"),      OP_COMPOSE_EDIT_TO },
  /* L10N: compose menu help line entry */
  { N_("CC"),      OP_COMPOSE_EDIT_CC },
  /* L10N: compose menu help line entry */
  { N_("Subj"),    OP_COMPOSE_EDIT_SUBJECT },
  { N_("Attach file"),  OP_COMPOSE_ATTACH_FILE },
  { N_("Descrip"), OP_COMPOSE_EDIT_DESCRIPTION },
  { N_("Help"),    OP_HELP },
  { NULL,	0 }
};

#ifdef USE_AUTOCRYPT
static const char *AutocryptRecUiFlags[] = {
  /* L10N: Autocrypt recommendation flag: off.
   * This is displayed when Autocrypt is turned off. */
  N_("Off"),
  /* L10N: Autocrypt recommendation flag: no.
   * This is displayed when Autocrypt cannot encrypt to the recipients. */
  N_("No"),
  /* L10N: Autocrypt recommendation flag: discouraged.
   * This is displayed when Autocrypt believes encryption should not be used.
   * This might occur if one of the recipient Autocrypt Keys has not been
   * used recently, or if the only key available is a Gossip Header key. */
  N_("Discouraged"),
  /* L10N: Autocrypt recommendation flag: available.
   * This is displayed when Autocrypt believes encryption is possible, but
   * leaves enabling it up to the sender.  Probably because "prefer encrypt"
   * is not set in both the sender and recipient keys. */
  N_("Available"),
  /* L10N: Autocrypt recommendation flag: yes.
   * This is displayed when Autocrypt would normally enable encryption
   * automatically. */
  N_("Yes"),
};
#endif

typedef struct
{
  HEADER *msg;
  BUFFER *fcc;
  SEND_CONTEXT *sctx;
#ifdef USE_AUTOCRYPT
  autocrypt_rec_t autocrypt_rec;
  int autocrypt_rec_override;
#endif
} compose_redraw_data_t;

static void calc_header_width_padding (int idx, const char *header, int calc_max)
{
  int width;

  HeaderPadding[idx] = mutt_strlen (header);
  width = mutt_strwidth (header);
  if (calc_max && MaxHeaderWidth < width)
    MaxHeaderWidth = width;
  HeaderPadding[idx] -= width;
}


/* The padding needed for each header is strlen() + max_width - strwidth().
 *
 * calc_header_width_padding sets each entry in HeaderPadding to
 * strlen - width.  Then, afterwards, we go through and add max_width
 * to each entry.
 */
static void init_header_padding (void)
{
  static short done = 0;
  int i;

  if (done)
    return;
  done = 1;

  for (i = 0; i < HDR_ATTACH_TITLE; i++)
  {
    if (i == HDR_CRYPTINFO)
      continue;
    calc_header_width_padding (i, _(Prompts[i]), 1);
  }

  /* Don't include "Sign as: " in the MaxHeaderWidth calculation.  It
   * doesn't show up by default, and so can make the indentation of
   * the other fields look funny. */
  calc_header_width_padding (HDR_CRYPTINFO, _(Prompts[HDR_CRYPTINFO]), 0);

  for (i = 0; i < HDR_ATTACH_TITLE; i++)
  {
    HeaderPadding[i] += MaxHeaderWidth;
    if (HeaderPadding[i] < 0)
      HeaderPadding[i] = 0;
  }
}

static void snd_entry (char *b, size_t blen, MUTTMENU *menu, int num)
{
  ATTACH_CONTEXT *actx = (ATTACH_CONTEXT *)menu->data;

  mutt_FormatString (b, blen, 0, MuttIndexWindow->cols, NONULL (AttachFormat),
                     mutt_attach_fmt,
                     actx->idx[actx->v2r[num]],
                     MUTT_FORMAT_STAT_FILE | MUTT_FORMAT_ARROWCURSOR);
}

#ifdef USE_AUTOCRYPT
static void autocrypt_compose_menu (HEADER *msg)
{
  char *prompt, *letters;
  int choice;

  msg->security |= APPLICATION_PGP;

  /* L10N:
     The compose menu autocrypt prompt.
     (e)ncrypt enables encryption via autocrypt.
     (c)lear sets cleartext.
     (a)utomatic defers to the recommendation.
  */
  prompt = _("Autocrypt: (e)ncrypt, (c)lear, (a)utomatic? ");

  /* L10N:
     The letter corresponding to the compose menu autocrypt prompt
     (e)ncrypt, (c)lear, (a)utomatic
   */
  letters = _("eca");

  choice = mutt_multi_choice (prompt, letters);
  switch (choice)
  {
    case 1:
      msg->security |= (AUTOCRYPT | AUTOCRYPT_OVERRIDE);
      msg->security &= ~(ENCRYPT | SIGN | OPPENCRYPT | INLINE);
      break;
    case 2:
      msg->security &= ~AUTOCRYPT;
      msg->security |= AUTOCRYPT_OVERRIDE;
      break;
    case 3:
      msg->security &= ~AUTOCRYPT_OVERRIDE;
      if (option (OPTCRYPTOPPORTUNISTICENCRYPT))
        msg->security |= OPPENCRYPT;
      break;
  }
}
#endif

static void redraw_crypt_lines (compose_redraw_data_t *rd)
{
  HEADER *msg = rd->msg;
  SEND_CONTEXT *sctx = rd->sctx;

  SETCOLOR (MT_COLOR_COMPOSE_HEADER);
  mutt_window_mvprintw (MuttIndexWindow, HDR_CRYPT, 0,
                        "%*s", HeaderPadding[HDR_CRYPT], _(Prompts[HDR_CRYPT]));
  NORMAL_COLOR;

  if ((WithCrypto & (APPLICATION_PGP | APPLICATION_SMIME)) == 0)
  {
    addstr(_("Not supported"));
    return;
  }

  if ((msg->security & (ENCRYPT | SIGN)) == (ENCRYPT | SIGN))
  {
    SETCOLOR (MT_COLOR_COMPOSE_SECURITY_BOTH);
    addstr (_("Sign, Encrypt"));
  }
  else if (msg->security & ENCRYPT)
  {
    SETCOLOR (MT_COLOR_COMPOSE_SECURITY_ENCRYPT);
    addstr (_("Encrypt"));
  }
  else if (msg->security & SIGN)
  {
    SETCOLOR (MT_COLOR_COMPOSE_SECURITY_SIGN);
    addstr (_("Sign"));
  }
  else
  {
    SETCOLOR (MT_COLOR_COMPOSE_SECURITY_NONE);
    addstr (_("None"));
  }
  NORMAL_COLOR;

  if ((msg->security & (ENCRYPT | SIGN)))
  {
    if ((WithCrypto & APPLICATION_PGP) && (msg->security & APPLICATION_PGP))
    {
      if ((msg->security & INLINE))
        addstr (_(" (inline PGP)"));
      else
        addstr (_(" (PGP/MIME)"));
    }
    else if ((WithCrypto & APPLICATION_SMIME) &&
             (msg->security & APPLICATION_SMIME))
      addstr (_(" (S/MIME)"));
  }

  if (option (OPTCRYPTOPPORTUNISTICENCRYPT) && (msg->security & OPPENCRYPT))
    addstr (_(" (OppEnc mode)"));

  mutt_window_clrtoeol (MuttIndexWindow);
  mutt_window_move (MuttIndexWindow, HDR_CRYPTINFO, 0);
  mutt_window_clrtoeol (MuttIndexWindow);

  if ((WithCrypto & APPLICATION_PGP)
      && (msg->security & APPLICATION_PGP) && (msg->security & SIGN))
  {
    SETCOLOR (MT_COLOR_COMPOSE_HEADER);
    printw ("%*s", HeaderPadding[HDR_CRYPTINFO], _(Prompts[HDR_CRYPTINFO]));
    NORMAL_COLOR;
    printw ("%s", sctx->pgp_sign_as ? sctx->pgp_sign_as :
            (PgpSignAs ? PgpSignAs : _("<default>")));
  }

  if ((WithCrypto & APPLICATION_SMIME)
      && (msg->security & APPLICATION_SMIME) && (msg->security & SIGN))
  {
    SETCOLOR (MT_COLOR_COMPOSE_HEADER);
    printw ("%*s", HeaderPadding[HDR_CRYPTINFO], _(Prompts[HDR_CRYPTINFO]));
    NORMAL_COLOR;
    printw ("%s", sctx->smime_sign_as ? sctx->smime_sign_as :
            (SmimeSignAs ? SmimeSignAs : _("<default>")));
  }

  /* Note: the smime crypt alg can be cleared in smime.c.
   * this causes a NULL sctx->smime_crypt_alg to override SmimeCryptAlg.
   */
  if ((WithCrypto & APPLICATION_SMIME)
      && (msg->security & APPLICATION_SMIME)
      && (msg->security & ENCRYPT)
      && (sctx->smime_crypt_alg ||
          (!sctx->smime_crypt_alg_cleared && SmimeCryptAlg)))
  {
    SETCOLOR (MT_COLOR_COMPOSE_HEADER);
    mutt_window_mvprintw (MuttIndexWindow, HDR_CRYPTINFO, 40, "%s", _("Encrypt with: "));
    NORMAL_COLOR;
    printw ("%s", sctx->smime_crypt_alg ? sctx->smime_crypt_alg : SmimeCryptAlg );
  }

#ifdef USE_AUTOCRYPT
  mutt_window_move (MuttIndexWindow, HDR_AUTOCRYPT, 0);
  mutt_window_clrtoeol (MuttIndexWindow);
  if (option (OPTAUTOCRYPT))
  {
    SETCOLOR (MT_COLOR_COMPOSE_HEADER);
    printw ("%*s", HeaderPadding[HDR_AUTOCRYPT], _(Prompts[HDR_AUTOCRYPT]));
    NORMAL_COLOR;
    if (msg->security & AUTOCRYPT)
    {
      SETCOLOR (MT_COLOR_COMPOSE_SECURITY_ENCRYPT);
      addstr (_("Encrypt"));
    }
    else
    {
      SETCOLOR (MT_COLOR_COMPOSE_SECURITY_NONE);
      addstr (_("Off"));
    }

    SETCOLOR (MT_COLOR_COMPOSE_HEADER);
    mutt_window_mvprintw (MuttIndexWindow, HDR_AUTOCRYPT, 40, "%s",
                          /* L10N:
                             The autocrypt compose menu Recommendation field.
                             Displays the output of the recommendation engine
                             (Off, No, Discouraged, Available, Yes)
                          */
                          _("Recommendation: "));
    NORMAL_COLOR;
    printw ("%s", _(AutocryptRecUiFlags[rd->autocrypt_rec]));
  }
#endif
}

static void update_crypt_info (compose_redraw_data_t *rd)
{
  HEADER *msg = rd->msg;

  if (option (OPTCRYPTOPPORTUNISTICENCRYPT))
    crypt_opportunistic_encrypt (msg);

#ifdef USE_AUTOCRYPT
  if (option (OPTAUTOCRYPT))
  {
    rd->autocrypt_rec = mutt_autocrypt_ui_recommendation (msg, NULL);

    /* Anything that enables ENCRYPT or SIGN, or turns on SMIME
     * overrides autocrypt, be it oppenc or the user having turned on
     * those flags manually. */
    if (msg->security & (ENCRYPT | SIGN | APPLICATION_SMIME))
      msg->security &= ~(AUTOCRYPT | AUTOCRYPT_OVERRIDE);
    else
    {
      if (!(msg->security & AUTOCRYPT_OVERRIDE))
      {
        if (rd->autocrypt_rec == AUTOCRYPT_REC_YES)
        {
          msg->security |= (AUTOCRYPT | APPLICATION_PGP);
          msg->security &= ~(INLINE | APPLICATION_SMIME);
        }
        else
          msg->security &= ~AUTOCRYPT;
      }
    }
  }
#endif

  redraw_crypt_lines (rd);
}


#ifdef MIXMASTER

static void redraw_mix_line (LIST *chain)
{
  size_t c;
  char *t;

  SETCOLOR (MT_COLOR_COMPOSE_HEADER);
  mutt_window_mvprintw (MuttIndexWindow, HDR_MIX, 0,
                        "%*s", HeaderPadding[HDR_MIX], _(Prompts[HDR_MIX]));
  NORMAL_COLOR;

  if (!chain)
  {
    addstr ("<no chain defined>");
    mutt_window_clrtoeol (MuttIndexWindow);
    return;
  }

  for (c = 12; chain; chain = chain->next)
  {
    t = chain->data;
    if (t && t[0] == '0' && t[1] == '\0')
      t = "<random>";

    if (c + mutt_strlen (t) + 2 >= MuttIndexWindow->cols)
      break;

    addstr (NONULL(t));
    if (chain->next)
      addstr (", ");

    c += mutt_strlen (t) + 2;
  }
}
#endif /* MIXMASTER */

static int
check_attachments(ATTACH_CONTEXT *actx)
{
  int i, r, rc = -1;
  struct stat st;
  BUFFER *pretty = NULL, *msg = NULL;

  for (i = 0; i < actx->idxlen; i++)
  {
    if (stat(actx->idx[i]->content->filename, &st) != 0)
    {
      if (!pretty)
        pretty = mutt_buffer_pool_get ();
      mutt_buffer_strcpy (pretty, actx->idx[i]->content->filename);
      mutt_buffer_pretty_mailbox (pretty);
      /* L10N:
         This message is displayed in the compose menu when an attachment
         doesn't stat.  %d is the attachment number and %s is the
         attachment filename.
         The filename is located last to avoid a long path hiding the
         error message.
      */
      mutt_error (_("Attachment #%d no longer exists: %s"),
                  i+1, mutt_b2s (pretty));
      goto cleanup;
    }

    if (actx->idx[i]->content->stamp < st.st_mtime)
    {
      if (!pretty)
        pretty = mutt_buffer_pool_get ();
      mutt_buffer_strcpy (pretty, actx->idx[i]->content->filename);
      mutt_buffer_pretty_mailbox (pretty);

      if (!msg)
        msg = mutt_buffer_pool_get ();
      /* L10N:
         This message is displayed in the compose menu when an attachment
         is modified behind the scenes.  %d is the attachment number
         and %s is the attachment filename.
         The filename is located last to avoid a long path hiding the
         prompt question.
      */
      mutt_buffer_printf (msg, _("Attachment #%d modified. Update encoding for %s?"),
                          i+1, mutt_b2s (pretty));

      if ((r = mutt_yesorno (mutt_b2s (msg), MUTT_YES)) == MUTT_YES)
	mutt_update_encoding (actx->idx[i]->content);
      else if (r == -1)
	goto cleanup;
    }
  }

  rc = 0;

cleanup:
  mutt_buffer_pool_release (&pretty);
  mutt_buffer_pool_release (&msg);
  return rc;
}

static void draw_envelope_addr (int line, ADDRESS *addr)
{
  char buf[LONG_STRING];

  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), addr, 1);
  SETCOLOR (MT_COLOR_COMPOSE_HEADER);
  mutt_window_mvprintw (MuttIndexWindow, line, 0,
                        "%*s", HeaderPadding[line], _(Prompts[line]));
  NORMAL_COLOR;
  mutt_paddstr (W, buf);
}

static void draw_envelope (compose_redraw_data_t *rd)
{
  HEADER *msg = rd->msg;
  const char *fcc = mutt_b2s (rd->fcc);

  draw_envelope_addr (HDR_FROM, msg->env->from);
  draw_envelope_addr (HDR_TO, msg->env->to);
  draw_envelope_addr (HDR_CC, msg->env->cc);
  draw_envelope_addr (HDR_BCC, msg->env->bcc);

  SETCOLOR (MT_COLOR_COMPOSE_HEADER);
  mutt_window_mvprintw (MuttIndexWindow, HDR_SUBJECT, 0,
                        "%*s", HeaderPadding[HDR_SUBJECT], _(Prompts[HDR_SUBJECT]));
  NORMAL_COLOR;
  mutt_paddstr (W, NONULL (msg->env->subject));

  draw_envelope_addr (HDR_REPLYTO, msg->env->reply_to);

  SETCOLOR (MT_COLOR_COMPOSE_HEADER);
  mutt_window_mvprintw (MuttIndexWindow, HDR_FCC, 0,
                        "%*s", HeaderPadding[HDR_FCC], _(Prompts[HDR_FCC]));
  NORMAL_COLOR;
  mutt_paddstr (W, fcc);

  if (WithCrypto)
    redraw_crypt_lines (rd);

#ifdef MIXMASTER
  redraw_mix_line (msg->chain);
#endif

  SETCOLOR (MT_COLOR_STATUS);
  mutt_window_mvaddstr (MuttIndexWindow, HDR_ATTACH_TITLE, 0, _("-- Attachments"));
  mutt_window_clrtoeol (MuttIndexWindow);

  NORMAL_COLOR;
}

static void edit_address_list (int line, ADDRESS **addr)
{
  char buf[HUGE_STRING] = ""; /* needs to be large for alias expansion */
  char *err = NULL;

  mutt_addrlist_to_local (*addr);
  rfc822_write_address (buf, sizeof (buf), *addr, 0);
  if (mutt_get_field (_(Prompts[line]), buf, sizeof (buf), MUTT_ALIAS) == 0)
  {
    rfc822_free_address (addr);
    *addr = mutt_parse_adrlist (*addr, buf);
    *addr = mutt_expand_aliases (*addr);
  }

  if (mutt_addrlist_to_intl (*addr, &err) != 0)
  {
    mutt_error (_("Warning: '%s' is a bad IDN."), err);
    mutt_refresh();
    FREE (&err);
  }

  /* redraw the expanded list so the user can see the result */
  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), *addr, 1);
  mutt_window_move (MuttIndexWindow, line, HDR_XOFFSET);
  mutt_paddstr (W, buf);
}

static int delete_attachment (ATTACH_CONTEXT *actx, int x)
{
  ATTACHPTR **idx = actx->idx;
  int rindex = actx->v2r[x];
  int y;

  if (rindex == 0 && actx->idxlen == 1)
  {
    mutt_error _("You may not delete the only attachment.");
    idx[rindex]->content->tagged = 0;
    return (-1);
  }

  if (rindex == 0 &&
      option (OPTCOMPOSECONFIRMDETACH) &&
      mutt_query_boolean (OPTCOMPOSECONFIRMDETACH,
  /* L10N:
     Prompt when trying to hit <detach-file> on the first entry in
     the compose menu.  This entry is most likely the message they just
     typed.  Hitting yes will remove the entry and unlink the file, so
     it's worth confirming they really meant to do it.
  */
                          _("Really delete the main message?"), 0) < 1)
  {
    idx[rindex]->content->tagged = 0;
    return (-1);
  }

  if (idx[rindex]->unowned)
    idx[rindex]->content->unlink = 0;

  for (y = 0; y < actx->idxlen; y++)
  {
    if (idx[y]->content->next == idx[rindex]->content)
    {
      idx[y]->content->next = idx[rindex]->content->next;
      break;
    }
  }

  idx[rindex]->content->next = NULL;
  /* mutt_make_message_attach() creates body->parts, shared by
   * body->hdr->content.  If we NULL out that, it creates a memory
   * leak because mutt_free_body() frees body->parts, not
   * body->hdr->content.
   *
   * Other mutt_send_message() message constructors are careful to free
   * any body->parts, removing depth:
   *  - mutt_prepare_template() used by postponed, resent, and draft files
   *  - mutt_copy_body() used by the recvattach menu and $forward_attachments.
   *
   * I believe it is safe to completely remove the "content->parts =
   * NULL" statement.  But for safety, am doing so only for the case
   * it must be avoided: message attachments.
   */
  if (!idx[rindex]->content->hdr)
    idx[rindex]->content->parts = NULL;
  mutt_free_body (&(idx[rindex]->content));
  FREE (&idx[rindex]->tree);
  FREE (&idx[rindex]);
  for (; rindex < actx->idxlen - 1; rindex++)
    idx[rindex] = idx[rindex+1];
  idx[actx->idxlen - 1] = NULL;
  actx->idxlen--;

  return (0);
}

/* The compose menu doesn't currently allow nested attachments.
 * However due to the shared structures and functions with recvattach,
 * most of the compose code at least works through the v2r table.
 *
 * The next three functions continue to support those abstractions,
 * but currently only allow sibling swapping.  Adding the additional
 * code to move an attachment across trees would be more complexity to
 * no purpose.
 */
static void swap_attachments (ATTACH_CONTEXT *actx, int cur_rindex, int next_rindex)
{
  BODY *prev = NULL, *cur, *next, *parent = NULL;
  int prev_rindex;
  ATTACHPTR *tmp;

  cur = actx->idx[cur_rindex]->content;
  next = actx->idx[next_rindex]->content;

  for (prev_rindex = 0; prev_rindex < cur_rindex; prev_rindex++)
  {
    if (actx->idx[prev_rindex]->content->parts == cur)
      parent = actx->idx[prev_rindex]->content;
    if (actx->idx[prev_rindex]->content->next == cur)
    {
      prev = actx->idx[prev_rindex]->content;
      break;
    }
  }

  if (prev)
    prev->next = next;
  else if (cur_rindex == 0)
    actx->hdr->content = next;
  else if (parent)
    parent->parts = next;

  cur->next = next->next;
  next->next = cur;

  tmp = actx->idx[cur_rindex];
  actx->idx[cur_rindex] = actx->idx[next_rindex];
  actx->idx[next_rindex] = tmp;
}

static int move_attachment_down (ATTACH_CONTEXT *actx, MUTTMENU *menu)
{
  int cur_rindex, next_rindex, next_vindex;
  BODY *cur, *next;

  cur_rindex = actx->v2r[menu->current];
  cur = actx->idx[cur_rindex]->content;

  next = cur->next;
  if (!next)
  {
    mutt_error _("You are on the last entry.");
    return -1;
  }
  for (next_rindex = cur_rindex + 1; next_rindex < actx->idxlen; next_rindex++)
  {
    if (actx->idx[next_rindex]->content == next)
      break;
  }
  if (next_rindex == actx->idxlen)
  {
    mutt_error _("You are on the last entry.");
    return -1;
  }

  swap_attachments (actx, cur_rindex, next_rindex);
  for (next_vindex = menu->current; next_vindex < actx->vcount; next_vindex++)
  {
    if (actx->v2r[next_vindex] == next_rindex)
    {
      menu->current = next_vindex;
      break;
    }
  }

  return 0;
}

static int move_attachment_up (ATTACH_CONTEXT *actx, MUTTMENU *menu)
{
  int prev_rindex, cur_rindex, prev_vindex;
  BODY *prev = NULL, *cur;

  cur_rindex = actx->v2r[menu->current];
  cur = actx->idx[cur_rindex]->content;

  for (prev_rindex = 0; prev_rindex < cur_rindex; prev_rindex++)
  {
    if (actx->idx[prev_rindex]->content->next == cur)
    {
      prev = actx->idx[prev_rindex]->content;
      break;
    }
  }
  if (!prev)
  {
    mutt_error _("You are on the first entry.");
    return -1;
  }

  swap_attachments (actx, prev_rindex, cur_rindex);
  for (prev_vindex = 0; prev_vindex < menu->current; prev_vindex++)
  {
    if (actx->v2r[prev_vindex] == prev_rindex)
    {
      menu->current = prev_vindex;
      break;
    }
  }

  return 0;
}


static void mutt_gen_compose_attach_list (ATTACH_CONTEXT *actx,
                                          BODY *m,
                                          int parent_type,
                                          int level)
{
  ATTACHPTR *new;

  for (; m; m = m->next)
  {
    if (m->type == TYPEMULTIPART && m->parts
        && (!(WithCrypto & APPLICATION_PGP) || !mutt_is_multipart_encrypted(m))
      )
    {
      mutt_gen_compose_attach_list (actx, m->parts, m->type, level);
    }
    else
    {
      new = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
      mutt_actx_add_attach (actx, new);
      new->content = m;
      m->aptr = new;
      new->parent_type = parent_type;
      new->level = level;

      /* We don't support multipart messages in the compose menu yet */
    }
  }
}

static void mutt_update_compose_menu (ATTACH_CONTEXT *actx, MUTTMENU *menu, int init)
{
  if (init)
  {
    mutt_gen_compose_attach_list (actx, actx->hdr->content, -1, 0);
    mutt_attach_init (actx);
    menu->data = actx;
  }

  mutt_update_tree (actx);

  menu->max = actx->vcount;
  if (menu->max)
  {
    if (menu->current >= menu->max)
      menu->current = menu->max - 1;
  }
  else
    menu->current = 0;

  menu->redraw |= REDRAW_INDEX | REDRAW_STATUS;
}

static void update_idx (MUTTMENU *menu, ATTACH_CONTEXT *actx, ATTACHPTR *new)
{
  new->level = (actx->idxlen > 0) ? actx->idx[actx->idxlen-1]->level : 0;
  if (actx->idxlen)
    actx->idx[actx->idxlen - 1]->content->next = new->content;
  new->content->aptr = new;
  mutt_actx_add_attach (actx, new);
  mutt_update_compose_menu (actx, menu, 0);
  menu->current = actx->vcount - 1;
}


/*
 * cum_attachs_size: Cumulative Attachments Size
 *
 * Returns the total number of bytes used by the attachments in the
 * attachment list _after_ content-transfer-encodings have been
 * applied.
 *
 */

static unsigned long cum_attachs_size (MUTTMENU *menu)
{
  size_t s;
  unsigned short i;
  ATTACH_CONTEXT *actx = menu->data;
  ATTACHPTR **idx = actx->idx;
  CONTENT *info;
  BODY *b;

  for (i = 0, s = 0; i < actx->idxlen; i++)
  {
    b = idx[i]->content;

    if (!b->content)
      b->content = mutt_get_content_info (b->filename, b);

    if ((info = b->content))
    {
      switch (b->encoding)
      {
	case ENCQUOTEDPRINTABLE:
	  s += 3 * (info->lobin + info->hibin) + info->ascii + info->crlf;
	  break;
	case ENCBASE64:
	  s += (4 * (info->lobin + info->hibin + info->ascii + info->crlf)) / 3;
	  break;
	default:
	  s += info->lobin + info->hibin + info->ascii + info->crlf;
	  break;
      }
    }
  }

  return s;
}

/* prototype for use below */
static void compose_status_line (char *buf, size_t buflen, size_t col, int cols, MUTTMENU *menu,
                                 const char *p);

/*
 * compose_format_str()
 *
 * %a = total number of attachments
 * %h = hostname  [option]
 * %l = approx. length of current message (in bytes)
 * %v = Mutt version
 *
 * This function is similar to status_format_str().  Look at that function for
 * help when modifying this function.
 */

static const char *
compose_format_str (char *buf, size_t buflen, size_t col, int cols, char op, const char *src,
                    const char *prefix, const char *ifstring,
                    const char *elsestring,
                    void *data, format_flag flags)
{
  char fmt[SHORT_STRING], tmp[SHORT_STRING];
  int optional = (flags & MUTT_FORMAT_OPTIONAL);
  MUTTMENU *menu = (MUTTMENU *) data;

  *buf = 0;
  switch (op)
  {
    case 'a': /* total number of attachments */
      snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
      snprintf (buf, buflen, fmt, menu->max);
      break;

    case 'h':  /* hostname */
      snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
      snprintf (buf, buflen, fmt, NONULL(Hostname));
      break;

    case 'l': /* approx length of current message in bytes */
      snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
      mutt_pretty_size (tmp, sizeof (tmp), menu ? cum_attachs_size(menu) : 0);
      snprintf (buf, buflen, fmt, tmp);
      break;

    case 'v':
      snprintf (fmt, sizeof (fmt), "Mutt %%s");
      snprintf (buf, buflen, fmt, MUTT_VERSION);
      break;

    case 0:
      *buf = 0;
      return (src);

    default:
      snprintf (buf, buflen, "%%%s%c", prefix, op);
      break;
  }

  if (optional)
    compose_status_line (buf, buflen, col, cols, menu, ifstring);
  else if (flags & MUTT_FORMAT_OPTIONAL)
    compose_status_line (buf, buflen, col, cols, menu, elsestring);

  return (src);
}

static void compose_status_line (char *buf, size_t buflen, size_t col, int cols,
                                 MUTTMENU *menu, const char *p)
{
  mutt_FormatString (buf, buflen, col, cols, p, compose_format_str,
                     menu, 0);
}

static void compose_menu_redraw (MUTTMENU *menu)
{
  char buf[LONG_STRING];
  compose_redraw_data_t *rd = menu->redraw_data;

  if (!rd)
    return;

  if (menu->redraw & REDRAW_FULL)
  {
    menu_redraw_full (menu);

    draw_envelope (rd);
    menu->offset = HDR_ATTACH;
    menu->pagelen = MuttIndexWindow->rows - HDR_ATTACH;
  }

  menu_check_recenter (menu);

  if (menu->redraw & REDRAW_STATUS)
  {
    compose_status_line (buf, sizeof (buf), 0, MuttStatusWindow->cols, menu, NONULL(ComposeFormat));
    mutt_window_move (MuttStatusWindow, 0, 0);
    SETCOLOR (MT_COLOR_STATUS);
    mutt_paddstr (MuttStatusWindow->cols, buf);
    NORMAL_COLOR;
    menu->redraw &= ~REDRAW_STATUS;
  }

#ifdef USE_SIDEBAR
  if (menu->redraw & REDRAW_SIDEBAR)
    menu_redraw_sidebar (menu);
#endif

  if (menu->redraw & REDRAW_INDEX)
    menu_redraw_index (menu);
  else if (menu->redraw & (REDRAW_MOTION | REDRAW_MOTION_RESYNCH))
    menu_redraw_motion (menu);
  else if (menu->redraw == REDRAW_CURRENT)
    menu_redraw_current (menu);
}


/* return values:
 *
 * 1	message should be postponed
 * 0	normal exit
 * -1	abort message
 * 2    edit was backgrounded
 */
int mutt_compose_menu (SEND_CONTEXT *sctx)
{
  HEADER *msg;   /* structure for new message */
  char helpstr[LONG_STRING];
  char buf[LONG_STRING];
  BUFFER *fname = NULL;
  MUTTMENU *menu;
  ATTACH_CONTEXT *actx;
  ATTACHPTR *new;
  int i, close = 0;
  int r = -1;		/* return value */
  int op = 0;
  int loop = 1;
  int fccSet = 0;	/* has the user edited the Fcc: field ? */
  CONTEXT *ctx = NULL, *this = NULL;
  /* Sort, SortAux could be changed in mutt_index_menu() */
  int oldSort, oldSortAux, oldSortThreadGroups;
  struct stat st;
  compose_redraw_data_t rd = {0};

  msg = sctx->msg;

  init_header_padding ();

  rd.msg = msg;
  rd.fcc = sctx->fcc;
  rd.sctx = sctx;

  menu = mutt_new_menu (MENU_COMPOSE);
  menu->offset = HDR_ATTACH;
  menu->make_entry = snd_entry;
  menu->tag = mutt_tag_attach;
  menu->help = mutt_compile_help (helpstr, sizeof (helpstr), MENU_COMPOSE, ComposeHelp);
  menu->custom_menu_redraw = compose_menu_redraw;
  menu->redraw_data = &rd;
  mutt_push_current_menu (menu);

  actx = safe_calloc (sizeof(ATTACH_CONTEXT), 1);
  actx->hdr = msg;
  mutt_update_compose_menu (actx, menu, 1);

  update_crypt_info (&rd);

  /* Since this is rather long lived, we don't use the pool */
  fname = mutt_buffer_new ();
  mutt_buffer_increase_size (fname, LONG_STRING);

  /* Another alternative would be to create a resume op and:
   *   mutt_unget_event (0, OP_COMPOSE_EDIT_MESSAGE_RESUME);
   */
  if (sctx->state)
  {
    if (sctx->state == SEND_STATE_COMPOSE_EDIT)
      goto edit_message_resume;
    if (sctx->state == SEND_STATE_COMPOSE_EDIT_HEADERS)
      goto edit_headers_resume;
    sctx->state = 0;
  }

  while (loop)
  {
    switch (op = mutt_menuLoop (menu))
    {
      case OP_COMPOSE_EDIT_FROM:
	edit_address_list (HDR_FROM, &msg->env->from);
        update_crypt_info (&rd);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
	break;
      case OP_COMPOSE_EDIT_TO:
	edit_address_list (HDR_TO, &msg->env->to);
        update_crypt_info (&rd);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;
      case OP_COMPOSE_EDIT_BCC:
	edit_address_list (HDR_BCC, &msg->env->bcc);
        update_crypt_info (&rd);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
	break;
      case OP_COMPOSE_EDIT_CC:
	edit_address_list (HDR_CC, &msg->env->cc);
        update_crypt_info (&rd);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;
      case OP_COMPOSE_EDIT_SUBJECT:
	if (msg->env->subject)
	  strfcpy (buf, msg->env->subject, sizeof (buf));
	else
	  buf[0] = 0;
	if (mutt_get_field (_("Subject: "), buf, sizeof (buf), 0) == 0)
	{
	  mutt_str_replace (&msg->env->subject, buf);
	  mutt_window_move (MuttIndexWindow, HDR_SUBJECT, HDR_XOFFSET);
	  if (msg->env->subject)
	    mutt_paddstr (W, msg->env->subject);
	  else
	    mutt_window_clrtoeol(MuttIndexWindow);
	}
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;
      case OP_COMPOSE_EDIT_REPLY_TO:
	edit_address_list (HDR_REPLYTO, &msg->env->reply_to);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
	break;
      case OP_COMPOSE_EDIT_FCC:
	mutt_buffer_strcpy (fname, mutt_b2s (sctx->fcc));
	if (mutt_buffer_get_field (_("Fcc: "), fname, MUTT_MAILBOX | MUTT_CLEAR) == 0)
	{
	  mutt_buffer_strcpy (sctx->fcc, mutt_b2s (fname));
	  mutt_buffer_pretty_multi_mailbox (sctx->fcc, FccDelimiter);
	  mutt_window_move (MuttIndexWindow, HDR_FCC, HDR_XOFFSET);
	  mutt_paddstr (W, mutt_b2s (sctx->fcc));
	  fccSet = 1;
	}
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;
      case OP_COMPOSE_EDIT_MESSAGE:
	if (Editor && (mutt_strcmp ("builtin", Editor) != 0) && !option (OPTEDITHDRS))
	{
          mutt_rfc3676_space_unstuff (msg);

          if ((sctx->flags & SENDBACKGROUNDEDIT) && option (OPTBACKGROUNDEDIT))
          {
            if (mutt_background_edit_file (sctx, Editor,
                                           msg->content->filename) == 2)
            {
              sctx->state = SEND_STATE_COMPOSE_EDIT;
              loop = 0;
              r = 2;
              break;
            }
          }
          else
            mutt_edit_file (Editor, msg->content->filename);
        edit_message_resume:
          sctx->state = 0;
          mutt_rfc3676_space_stuff (msg);
	  mutt_update_encoding (msg->content);
	  menu->redraw = REDRAW_FULL;
	  mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
	  break;
	}
	/* fall through */
      case OP_COMPOSE_EDIT_HEADERS:
        mutt_rfc3676_space_unstuff (msg);

	if (mutt_strcmp ("builtin", Editor) != 0 &&
	    (op == OP_COMPOSE_EDIT_HEADERS ||
             (op == OP_COMPOSE_EDIT_MESSAGE && option (OPTEDITHDRS))))
	{
	  char *tag = NULL, *err = NULL;
	  mutt_env_to_local (msg->env);

          if ((sctx->flags & SENDBACKGROUNDEDIT) && option (OPTBACKGROUNDEDIT))
          {
            if (mutt_edit_headers (Editor, sctx, MUTT_EDIT_HEADERS_BACKGROUND) == 2)
            {
              sctx->state = SEND_STATE_COMPOSE_EDIT_HEADERS;
              loop = 0;
              r = 2;
              break;
            }
          }
          else
            mutt_edit_headers (NONULL (Editor), sctx, 0);

        edit_headers_resume:
          if (sctx->state == SEND_STATE_COMPOSE_EDIT_HEADERS)
          {
            mutt_edit_headers (Editor, sctx, MUTT_EDIT_HEADERS_RESUME);
            sctx->state = 0;
          }
	  if (mutt_env_to_intl (msg->env, &tag, &err))
	  {
	    mutt_error (_("Bad IDN in \"%s\": '%s'"), tag, err);
	    FREE (&err);
	  }
          update_crypt_info (&rd);
	}
	else
	{
	  /* this is grouped with OP_COMPOSE_EDIT_HEADERS because the
	     attachment list could change if the user invokes ~v to edit
	     the message with headers, in which we need to execute the
	     code below to regenerate the index array */
	  mutt_builtin_editor (sctx);
	}

        mutt_rfc3676_space_stuff (msg);
	mutt_update_encoding (msg->content);

	/* attachments may have been added */
	if (actx->idxlen && actx->idx[actx->idxlen - 1]->content->next)
	{
          mutt_actx_free_entries (actx);
          mutt_update_compose_menu (actx, menu, 1);
	}

        menu->redraw = REDRAW_FULL;
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
	break;



      case OP_COMPOSE_ATTACH_KEY:
        if (!(WithCrypto & APPLICATION_PGP))
          break;

	new = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
	if ((new->content = crypt_pgp_make_key_attachment()) != NULL)
	{
	  update_idx (menu, actx, new);
	  menu->redraw |= REDRAW_INDEX;
	}
	else
	  FREE (&new);

	menu->redraw |= REDRAW_STATUS;

        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;


      case OP_COMPOSE_ATTACH_FILE:
      {
        char *prompt, **files;
        int error, numfiles;

        prompt = _("Attach file");
        numfiles = 0;
        files = NULL;

        if (mutt_enter_filenames (prompt, &files, &numfiles) == -1)
          break;

        error = 0;
        if (numfiles > 1)
          mutt_message _("Attaching selected files...");
        for (i = 0; i < numfiles; i++)
        {
          char *att = files[i];
          if (!att)
            continue;
          new = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
          new->unowned = 1;
          new->content = mutt_make_file_attach (att);
          if (new->content != NULL)
            update_idx (menu, actx, new);
          else
          {
            error = 1;
            mutt_error (_("Unable to attach %s!"), att);
            FREE (&new);
          }
          FREE (&files[i]);
        }

        FREE (&files);
        if (!error) mutt_clear_error ();

        menu->redraw |= REDRAW_INDEX | REDRAW_STATUS;
      }
      mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
      break;

      case OP_COMPOSE_ATTACH_MESSAGE:
      {
        char *prompt;
        HEADER *h;

        mutt_buffer_clear (fname);
        prompt = _("Open mailbox to attach message from");

        if (Context)
        {
          mutt_buffer_strcpy (fname, NONULL (Context->path));
          mutt_buffer_pretty_mailbox (fname);
        }

        if ((mutt_enter_mailbox (prompt, fname, 0) == -1) ||
            !mutt_buffer_len (fname))
          break;

        mutt_buffer_expand_path (fname);
#ifdef USE_IMAP
        if (!mx_is_imap (mutt_b2s (fname)))
#endif
#ifdef USE_POP
          if (!mx_is_pop (mutt_b2s (fname)))
#endif
            /* check to make sure the file exists and is readable */
            if (access (mutt_b2s (fname), R_OK) == -1)
            {
              mutt_perror (mutt_b2s (fname));
              break;
            }

        menu->redraw = REDRAW_FULL;

        ctx = mx_open_mailbox (mutt_b2s (fname), MUTT_READONLY, NULL);
        if (ctx == NULL)
        {
          mutt_error (_("Unable to open mailbox %s"), mutt_b2s (fname));
          break;
        }

        if (!ctx->msgcount)
        {
          mx_close_mailbox (ctx, NULL);
          FREE (&ctx);
          mutt_error _("No messages in that folder.");
          break;
        }

        this = Context; /* remember current folder and sort methods*/
        oldSort = Sort;
        oldSortAux = SortAux;
        oldSortThreadGroups = SortThreadGroups;

        Context = ctx;
        set_option(OPTATTACHMSG);
        mutt_message _("Tag the messages you want to attach!");
        close = mutt_index_menu ();
        unset_option(OPTATTACHMSG);

        if (!Context)
        {
          /* go back to the folder we started from */
          Context = this;
          /* Restore old $sort and $sort_aux */
          Sort = oldSort;
          SortAux = oldSortAux;
          SortThreadGroups = oldSortThreadGroups;
          menu->redraw |= REDRAW_INDEX | REDRAW_STATUS;
          break;
        }

        for (i = 0; i < Context->msgcount; i++)
        {
          h = Context->hdrs[i];
          if (h->tagged)
          {
            new = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
            new->content = mutt_make_message_attach (Context, h, 1);
            if (new->content != NULL)
              update_idx (menu, actx, new);
            else
            {
              mutt_error _("Unable to attach!");
              FREE (&new);
            }
          }
        }
        menu->redraw |= REDRAW_FULL;

        if (close == OP_QUIT)
          mx_close_mailbox (Context, NULL);
        else
          mx_fastclose_mailbox (Context);
        FREE (&Context);

        /* go back to the folder we started from */
        Context = this;
        /* Restore old $sort and $sort_aux */
        Sort = oldSort;
        SortAux = oldSortAux;
        SortThreadGroups = oldSortThreadGroups;
      }
      mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
      break;

      case OP_DELETE:
	CHECK_COUNT;
	if (delete_attachment (actx, menu->current) == -1)
	  break;
	mutt_update_compose_menu (actx, menu, 0);
	if (menu->current == 0)
	  msg->content = actx->idx[0]->content;

        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;

      case OP_COMPOSE_MOVE_DOWN:
        CHECK_COUNT;
        if (move_attachment_down (actx, menu) == -1)
          break;
	mutt_update_compose_menu (actx, menu, 0);
        break;

      case OP_COMPOSE_MOVE_UP:
        CHECK_COUNT;
        if (move_attachment_up (actx, menu) == -1)
          break;
	mutt_update_compose_menu (actx, menu, 0);
        break;

      case OP_COMPOSE_TOGGLE_RECODE:
      {
        CHECK_COUNT;
        if (!mutt_is_text_part (CURATTACH->content))
        {
	  mutt_error (_("Recoding only affects text attachments."));
	  break;
	}
        CURATTACH->content->noconv = !CURATTACH->content->noconv;
        if (CURATTACH->content->noconv)
	  mutt_message (_("The current attachment won't be converted."));
        else
	  mutt_message (_("The current attachment will be converted."));
	menu->redraw = REDRAW_CURRENT;
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;
      }

      case OP_COMPOSE_EDIT_DESCRIPTION:
	CHECK_COUNT;
	strfcpy (buf,
		 CURATTACH->content->description ?
		 CURATTACH->content->description : "",
		 sizeof (buf));
	/* header names should not be translated */
	if (mutt_get_field ("Description: ", buf, sizeof (buf), 0) == 0)
	{
	  mutt_str_replace (&CURATTACH->content->description, buf);
	  menu->redraw = REDRAW_CURRENT;
	}
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;

      case OP_COMPOSE_UPDATE_ENCODING:
        CHECK_COUNT;
        if (menu->tagprefix)
        {
	  BODY *top;
	  for (top = msg->content; top; top = top->next)
	  {
	    if (top->tagged)
	      mutt_update_encoding (top);
	  }
	  menu->redraw = REDRAW_FULL;
	}
        else
        {
          mutt_update_encoding(CURATTACH->content);
	  menu->redraw = REDRAW_CURRENT | REDRAW_STATUS;
	}
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;

      case OP_COMPOSE_TOGGLE_DISPOSITION:
	/* toggle the content-disposition between inline/attachment */
	CURATTACH->content->disposition = (CURATTACH->content->disposition == DISPINLINE) ? DISPATTACH : DISPINLINE;
	menu->redraw = REDRAW_CURRENT;
	break;

      case OP_EDIT_TYPE:
	CHECK_COUNT;
        {
	  mutt_edit_content_type (NULL, CURATTACH->content, NULL);

	  /* this may have been a change to text/something */
	  mutt_update_encoding (CURATTACH->content);

	  menu->redraw = REDRAW_CURRENT;
	}
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;

      case OP_COMPOSE_EDIT_ENCODING:
	CHECK_COUNT;
	strfcpy (buf, ENCODING (CURATTACH->content->encoding),
                 sizeof (buf));
	if (mutt_get_field ("Content-Transfer-Encoding: ", buf,
                            sizeof (buf), 0) == 0 && buf[0])
	{
	  if ((i = mutt_check_encoding (buf)) != ENCOTHER && i != ENCUUENCODED)
	  {
	    CURATTACH->content->encoding = i;
	    menu->redraw = REDRAW_CURRENT | REDRAW_STATUS;
	    mutt_clear_error();
	  }
	  else
	    mutt_error _("Invalid encoding.");
	}
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;

      case OP_COMPOSE_SEND_MESSAGE:

        /* Note: We don't invoke send2-hook here, since we want to leave
	 * users an opportunity to change settings from the ":" prompt.
	 */

        if (check_attachments(actx) != 0)
        {
	  menu->redraw = REDRAW_FULL;
	  break;
	}


#ifdef MIXMASTER
        if (msg->chain && mix_check_message (msg) != 0)
	  break;
#endif

	if (!fccSet && mutt_buffer_len (sctx->fcc))
	{
	  if ((i = query_quadoption (OPT_COPY,
                                     _("Save a copy of this message?"))) == -1)
	    break;
	  else if (i == MUTT_NO)
	    mutt_buffer_clear (sctx->fcc);
	}

	loop = 0;
	r = 0;
	break;

      case OP_COMPOSE_EDIT_FILE:
	CHECK_COUNT;
	mutt_edit_file (NONULL(Editor), CURATTACH->content->filename);
	mutt_update_encoding (CURATTACH->content);
	menu->redraw = REDRAW_CURRENT | REDRAW_STATUS;
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
	break;

      case OP_COMPOSE_TOGGLE_UNLINK:
	CHECK_COUNT;
	CURATTACH->content->unlink = !CURATTACH->content->unlink;
	menu->redraw = REDRAW_INDEX;
        /* No send2hook since this doesn't change the message. */
	break;

      case OP_COMPOSE_GET_ATTACHMENT:
        CHECK_COUNT;
        if (menu->tagprefix)
        {
	  BODY *top;
	  for (top = msg->content; top; top = top->next)
	  {
	    if (top->tagged)
	      mutt_get_tmp_attachment(top);
	  }
	  menu->redraw = REDRAW_FULL;
	}
        else if (mutt_get_tmp_attachment(CURATTACH->content) == 0)
	  menu->redraw = REDRAW_CURRENT;

        /* No send2hook since this doesn't change the message. */
        break;

      case OP_COMPOSE_RENAME_ATTACHMENT:
      {
        char *src;
        int ret;

        CHECK_COUNT;
        if (CURATTACH->content->d_filename)
          src = CURATTACH->content->d_filename;
        else
          src = CURATTACH->content->filename;
        mutt_buffer_strcpy (fname, mutt_basename (NONULL (src)));
        ret = mutt_buffer_get_field (_("Send attachment with name: "),
                                     fname, MUTT_FILE);
        if (ret == 0)
        {
          /*
           * As opposed to RENAME_FILE, we don't check fname[0] because it's
           * valid to set an empty string here, to erase what was set
           */
          mutt_str_replace (&CURATTACH->content->d_filename, mutt_b2s (fname));
          menu->redraw = REDRAW_CURRENT;
        }
      }
      break;

      case OP_COMPOSE_RENAME_FILE:
	CHECK_COUNT;
	mutt_buffer_strcpy (fname, CURATTACH->content->filename);
	mutt_buffer_pretty_mailbox (fname);

	if ((mutt_buffer_get_field (_("Rename to: "), fname, MUTT_FILE) == 0) &&
            mutt_buffer_len (fname))
        {
          if (stat(CURATTACH->content->filename, &st) == -1)
          {
            /* L10N:
               "stat" is a system call. Do "man 2 stat" for more information. */
            mutt_error (_("Can't stat %s: %s"), mutt_b2s (fname), strerror (errno));
            break;
          }

          mutt_buffer_expand_path (fname);
          if (mutt_rename_file (CURATTACH->content->filename, mutt_b2s (fname)))
            break;

          mutt_str_replace (&CURATTACH->content->filename, mutt_b2s (fname));
          menu->redraw = REDRAW_CURRENT;

          if (CURATTACH->content->stamp >= st.st_mtime)
            mutt_stamp_attachment(CURATTACH->content);
        }

        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;

      case OP_COMPOSE_NEW_MIME:
      {
        char type[STRING];
        char *p;
        int itype;
        FILE *fp;

        mutt_window_clearline (MuttMessageWindow, 0);
        mutt_buffer_clear (fname);
        if ((mutt_buffer_get_field (_("New file: "), fname, MUTT_FILE) != 0) ||
            !mutt_buffer_len (fname))
          continue;
        mutt_buffer_expand_path (fname);

        /* Call to lookup_mime_type () ?  maybe later */
        type[0] = 0;
        if (mutt_get_field ("Content-Type: ", type, sizeof (type), 0) != 0
            || !type[0])
          continue;

        if (!(p = strchr (type, '/')))
        {
          mutt_error _("Content-Type is of the form base/sub");
          continue;
        }
        *p++ = 0;
        if ((itype = mutt_check_mime_type (type)) == TYPEOTHER)
        {
          mutt_error (_("Unknown Content-Type %s"), type);
          continue;
        }

        new = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
        /* Touch the file */
        if (!(fp = safe_fopen (mutt_b2s (fname), "w")))
        {
          mutt_error (_("Can't create file %s"), mutt_b2s (fname));
          FREE (&new);
          continue;
        }
        safe_fclose (&fp);

        if ((new->content = mutt_make_file_attach (mutt_b2s (fname))) == NULL)
        {
          /* L10N:
             This phrase is a modified quote originally from Cool Hand
             Luke, intended to be somewhat humorous.
          */
          mutt_error _("What we have here is a failure to make an attachment");
          FREE (&new);
          continue;
        }
        update_idx (menu, actx, new);

        CURATTACH->content->type = itype;
        mutt_str_replace (&CURATTACH->content->subtype, p);
        CURATTACH->content->unlink = 1;
        menu->redraw |= REDRAW_INDEX | REDRAW_STATUS;

        if (mutt_compose_attachment (CURATTACH->content))
        {
          mutt_update_encoding (CURATTACH->content);
          menu->redraw = REDRAW_FULL;
        }
      }
      mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
      break;

      case OP_COMPOSE_EDIT_MIME:
	CHECK_COUNT;
	if (mutt_edit_attachment (CURATTACH->content))
	{
	  mutt_update_encoding (CURATTACH->content);
	  menu->redraw = REDRAW_FULL;
	}
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;

      case OP_COMPOSE_VIEW_ALT:
      case OP_COMPOSE_VIEW_ALT_TEXT:
      case OP_COMPOSE_VIEW_ALT_MAILCAP:
      case OP_COMPOSE_VIEW_ALT_PAGER:
      {
        BODY *alternative;

        if (!SendMultipartAltFilter)
        {
          mutt_error _("$send_multipart_alternative_filter is not set");
          break;
        }
        alternative = mutt_run_send_alternative_filter (msg);
        if (!alternative)
          break;
	switch (op)
	{
	  case OP_COMPOSE_VIEW_ALT_TEXT:
	    op = MUTT_AS_TEXT;
	    break;
	  case OP_COMPOSE_VIEW_ALT_MAILCAP:
	    op = MUTT_MAILCAP;
	    break;
	  case OP_COMPOSE_VIEW_ALT_PAGER:
	    op = MUTT_VIEW_PAGER;
	    break;
	  default:
	    op = MUTT_REGULAR;
	    break;
	}
        mutt_view_attachment (NULL, alternative, op, NULL, actx);
        mutt_free_body (&alternative);
        break;
      }

      case OP_ATTACH_VIEW_MAILCAP:
	mutt_view_attachment (NULL, CURATTACH->content, MUTT_MAILCAP,
			      NULL, actx);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_ATTACH_VIEW_TEXT:
	mutt_view_attachment (NULL, CURATTACH->content, MUTT_AS_TEXT,
			      NULL, actx);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_ATTACH_VIEW_PAGER:
	mutt_view_attachment (NULL, CURATTACH->content, MUTT_VIEW_PAGER,
			      NULL, actx);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_VIEW_ATTACH:
      case OP_DISPLAY_HEADERS:
	CHECK_COUNT;
	mutt_attach_display_loop (menu, op, NULL, actx, 0);
	menu->redraw = REDRAW_FULL;
        /* no send2hook, since this doesn't modify the message */
	break;

      case OP_SAVE:
	CHECK_COUNT;
	mutt_save_attachment_list (actx, NULL, menu->tagprefix, CURATTACH->content, NULL, menu);
        /* no send2hook, since this doesn't modify the message */
	break;

      case OP_PRINT:
	CHECK_COUNT;
	mutt_print_attachment_list (actx, NULL, menu->tagprefix, CURATTACH->content);
        /* no send2hook, since this doesn't modify the message */
	break;

      case OP_PIPE:
      case OP_FILTER:
        CHECK_COUNT;
	mutt_pipe_attachment_list (actx, NULL, menu->tagprefix, CURATTACH->content, op == OP_FILTER);
	if (op == OP_FILTER) /* cte might have changed */
	  menu->redraw = menu->tagprefix ? REDRAW_FULL : REDRAW_CURRENT;
        menu->redraw |= REDRAW_STATUS;
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
	break;

      case OP_EXIT:
	if ((i = query_quadoption (OPT_POSTPONE, _("Postpone this message?"))) == MUTT_NO)
	{
          for (i = 0; i < actx->idxlen; i++)
            if (actx->idx[i]->unowned)
              actx->idx[i]->content->unlink = 0;

          if (!(sctx->flags & SENDNOFREEHEADER))
          {
            for (i = 0; i < actx->idxlen; i++)
            {
              /* avoid freeing other attachments */
              actx->idx[i]->content->next = NULL;
              /* See the comment in delete_attachment() */
              if (!actx->idx[i]->content->hdr)
                actx->idx[i]->content->parts = NULL;
              mutt_free_body (&actx->idx[i]->content);
            }
          }
	  r = -1;
	  loop = 0;
	  break;
	}
	else if (i == -1)
	  break; /* abort */

	/* fall through */

      case OP_COMPOSE_POSTPONE_MESSAGE:

        if (check_attachments(actx) != 0)
        {
	  menu->redraw = REDRAW_FULL;
	  break;
	}

	loop = 0;
	r = 1;
	break;

      case OP_COMPOSE_ISPELL:
	endwin ();
	snprintf (buf, sizeof (buf), "%s -x %s", NONULL(Ispell), msg->content->filename);
	if (mutt_system (buf) == -1)
	  mutt_error (_("Error running \"%s\"!"), buf);
	else
        {
	  mutt_update_encoding (msg->content);
	  menu->redraw |= REDRAW_STATUS;
	}
	break;

      case OP_COMPOSE_WRITE_MESSAGE:

        mutt_buffer_clear (fname);
        if (Context)
        {
          mutt_buffer_strcpy (fname, NONULL (Context->path));
          mutt_buffer_pretty_mailbox (fname);
        }
        if (actx->idxlen)
          msg->content = actx->idx[0]->content;
        if ((mutt_enter_mailbox (_("Write message to mailbox"), fname, 0) != -1) &&
            mutt_buffer_len (fname))
        {
          mutt_message (_("Writing message to %s ..."), mutt_b2s (fname));
          mutt_buffer_expand_path (fname);

          if (msg->content->next)
            msg->content = mutt_make_multipart_mixed (msg->content);

          if (mutt_write_fcc (mutt_b2s (fname), sctx, NULL, 0, NULL) == 0)
            mutt_message _("Message written.");

          msg->content = mutt_remove_multipart_mixed (msg->content);
        }
        break;



      case OP_COMPOSE_PGP_MENU:
        if (!(WithCrypto & APPLICATION_PGP))
          break;
        if (!crypt_has_module_backend (APPLICATION_PGP))
        {
          mutt_error _("No PGP backend configured");
          break;
        }
	if ((WithCrypto & APPLICATION_SMIME)
            && (msg->security & APPLICATION_SMIME))
	{
          if (msg->security & (ENCRYPT | SIGN))
          {
            if (mutt_yesorno (_("S/MIME already selected. Clear & continue ? "),
                              MUTT_YES) != MUTT_YES)
            {
              mutt_clear_error ();
              break;
            }
            msg->security &= ~(ENCRYPT | SIGN);
          }
	  msg->security &= ~APPLICATION_SMIME;
	  msg->security |= APPLICATION_PGP;
          update_crypt_info (&rd);
	}
	crypt_pgp_send_menu (sctx);
	update_crypt_info (&rd);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;


      case OP_FORGET_PASSPHRASE:
	crypt_forget_passphrase ();
	break;


      case OP_COMPOSE_SMIME_MENU:
        if (!(WithCrypto & APPLICATION_SMIME))
          break;
        if (!crypt_has_module_backend (APPLICATION_SMIME))
        {
          mutt_error _("No S/MIME backend configured");
          break;
        }

	if ((WithCrypto & APPLICATION_PGP)
            && (msg->security & APPLICATION_PGP))
	{
          if (msg->security & (ENCRYPT | SIGN))
          {
            if (mutt_yesorno (_("PGP already selected. Clear & continue ? "),
                              MUTT_YES) != MUTT_YES)
            {
              mutt_clear_error ();
              break;
            }
            msg->security &= ~(ENCRYPT | SIGN);
          }
	  msg->security &= ~APPLICATION_PGP;
	  msg->security |= APPLICATION_SMIME;
          update_crypt_info (&rd);
	}
	crypt_smime_send_menu (sctx);
	update_crypt_info (&rd);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;


#ifdef MIXMASTER
      case OP_COMPOSE_MIX:

      	mix_make_chain (&msg->chain);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;
#endif

#ifdef USE_AUTOCRYPT
      case OP_COMPOSE_AUTOCRYPT_MENU:
        if (!option (OPTAUTOCRYPT))
          break;

	if ((WithCrypto & APPLICATION_SMIME)
            && (msg->security & APPLICATION_SMIME))
	{
          if (msg->security & (ENCRYPT | SIGN))
          {
            if (mutt_yesorno (_("S/MIME already selected. Clear & continue ? "),
                              MUTT_YES) != MUTT_YES)
            {
              mutt_clear_error ();
              break;
            }
            msg->security &= ~(ENCRYPT | SIGN);
          }
	  msg->security &= ~APPLICATION_SMIME;
	  msg->security |= APPLICATION_PGP;
          update_crypt_info (&rd);
	}
	autocrypt_compose_menu (msg);
	update_crypt_info (&rd);
        mutt_message_hook (NULL, msg, MUTT_SEND2HOOK);
        break;
#endif
    }
  }

  mutt_buffer_free (&fname);

#ifdef USE_AUTOCRYPT
  /* This is a fail-safe to make sure the bit isn't somehow turned
   * on.  The user could have disabled the option after setting AUTOCRYPT,
   * or perhaps resuming or replying to an autocrypt message.
   */
  if (!option (OPTAUTOCRYPT))
    msg->security &= ~AUTOCRYPT;
#endif

  mutt_pop_current_menu (menu);
  mutt_menuDestroy (&menu);

  if (actx->idxlen)
    msg->content = actx->idx[0]->content;
  else
    msg->content = NULL;

  mutt_free_attach_context (&actx);

  return (r);
}
