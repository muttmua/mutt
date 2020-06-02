/*
 * Copyright (C) 2020 Kevin J. McCarthy <kevin@8t8.us>
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

#ifndef _SEND_H
#define _SEND_H 1

enum
{
  SEND_STATE_FIRST_EDIT = 1,
  SEND_STATE_FIRST_EDIT_HEADERS,
  SEND_STATE_COMPOSE_EDIT,
  SEND_STATE_COMPOSE_EDIT_HEADERS
};

typedef struct send_scope
{
  unsigned char options[(OPTMAX + 7)/8];
  unsigned char quadoptions[(OPT_MAX*2 + 7) / 8];

  char *maildir;  /* $folder */
  char *outbox;
  char *postponed;
  ADDRESS *env_from;
  ADDRESS *from;
  char *sendmail;
#if USE_SMTP
  char *smtp_url;
#endif

  /* We store these because if the send menu isn't altered, we
   * want to preserve the original scope fallback values. */
  char *pgp_sign_as;
  char *smime_sign_as;
  char *smime_crypt_alg;
} SEND_SCOPE;

typedef struct send_ctx
{
  int flags;
  int state;

  HEADER *msg;
  BUFFER *fcc;
  BUFFER *tempfile;
  time_t mtime;
  time_t tempfile_mtime;
  char *date_header;

  /* Note: cur is set to NULL if the session is backgrounded. */
  HEADER *cur;
  unsigned int has_cur : 1;
  unsigned int is_backgrounded : 1;
  unsigned int cur_security;
  char *cur_message_id;
  char *ctx_realpath;
  LIST *tagged_message_ids;

  SEND_SCOPE *global_scope;
  SEND_SCOPE *local_scope;

  /* These store the values set from the send menu */
  char *pgp_sign_as;
  char *smime_sign_as;
  char *smime_crypt_alg;
  unsigned int smime_crypt_alg_cleared : 1;
} SEND_CONTEXT;

ADDRESS *mutt_remove_xrefs (ADDRESS *, ADDRESS *);
int mutt_edit_address (ADDRESS **, const char *, int);
void mutt_forward_intro (CONTEXT *ctx, HEADER *cur, FILE *fp);
void mutt_forward_trailer (CONTEXT *ctx, HEADER *cur, FILE *fp);
void mutt_make_attribution (CONTEXT *ctx, HEADER *cur, FILE *out);
void mutt_make_post_indent (CONTEXT *ctx, HEADER *cur, FILE *out);
int mutt_fetch_recips (ENVELOPE *out, ENVELOPE *in, int flags);
void mutt_fix_reply_recipients (ENVELOPE *env);
void mutt_make_forward_subject (ENVELOPE *env, CONTEXT *ctx, HEADER *cur);
void mutt_make_misc_reply_headers (ENVELOPE *env, CONTEXT *ctx, HEADER *cur, ENVELOPE *curenv);
void mutt_add_to_reference_headers (ENVELOPE *env, ENVELOPE *curenv, LIST ***pp, LIST ***qq);
void mutt_set_followup_to (ENVELOPE *);
ADDRESS *mutt_default_from (void);
void mutt_encode_descriptions (BODY *, short);
int mutt_resend_message (FILE *, CONTEXT *, HEADER *);
int mutt_send_message (int, HEADER *, const char *, CONTEXT *, HEADER *);
int mutt_send_message_resume (SEND_CONTEXT **psctx);

#endif
