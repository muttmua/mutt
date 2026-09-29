/*
 * Copyright (C) 2000-2007 Brendan Cully <brendan@kublai.com>
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

/* remote host account manipulation (POP/IMAP) */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "account.h"
#include "url.h"
#include "mutt_curses.h"

/* Accounts aren't independently allocated.  They are members of the
 * CONNECTION and IMAP_DATA structs.  This function frees the char*
 * members of the ACCOUNT and resets the integer values in case the
 * structure is reused in a loop.
 */
void mutt_account_free(ACCOUNT *account)
{
  if (!account)
    return;

  FREE(&account->user);
  FREE(&account->login);
  FREE(&account->pass);
  FREE(&account->host);
  account->port = 0;
  account->type = 0;
  account->flags = 0;
}

void mutt_account_copy(ACCOUNT *dest, const ACCOUNT *src)
{
  if (!dest || !src)
    return;

  dest->user = safe_strdup(src->user);
  dest->login = safe_strdup(src->login);
  dest->pass = safe_strdup(src->pass);
  dest->host = safe_strdup(src->host);
  dest->port = src->port;
  dest->type = src->type;
  dest->flags = src->flags;
}


/* mutt_account_match: compare account info (host/port/user) */
int mutt_account_match(const ACCOUNT *a1, const ACCOUNT *a2)
{
  const char *user = NONULL(Username);

  if (a1->type != a2->type)
    return 0;
  if (ascii_strcasecmp(a1->host, a2->host))
    return 0;
  if (a1->port != a2->port)
    return 0;

#ifdef USE_IMAP
  if (a1->type == MUTT_ACCT_TYPE_IMAP)
  {
    if (ImapUser)
      user = ImapUser;
  }
#endif

#ifdef USE_POP
  if (a1->type == MUTT_ACCT_TYPE_POP && PopUser)
    user = PopUser;
#endif

  if (a1->flags & a2->flags & MUTT_ACCT_USER)
    return (!mutt_strcmp(a1->user, a2->user));
  if (a1->flags & MUTT_ACCT_USER)
    return (!mutt_strcmp(a1->user, user));
  if (a2->flags & MUTT_ACCT_USER)
    return (!mutt_strcmp(a2->user, user));

  return 1;
}

/* mutt_account_fromurl: fill account with information from url. */
int mutt_account_fromurl(ACCOUNT *account, ciss_url_t *url)
{
  /* must be present */
  if (url->host)
    mutt_str_replace(&account->host, url->host);
  else
    return -1;

  if (url->user)
  {
    mutt_str_replace(&account->user, url->user);
    account->flags |= MUTT_ACCT_USER;
    account->flags |= MUTT_ACCT_USER_FROM_URL;
  }
  if (url->pass)
  {
    mutt_str_replace(&account->pass, url->pass);
    account->flags |= MUTT_ACCT_PASS;
    account->flags |= MUTT_ACCT_PASS_FROM_URL;
  }
  if (url->port)
  {
    account->port = url->port;
    account->flags |= MUTT_ACCT_PORT;
  }

  return 0;
}

/* Fill URL with info from account. The URL information
 * is a set of pointers into account - don't free or edit account until
 * you've finished with url (make a copy of account if you need it for
 * a while).
 *
 * By default "url" will be populated with the same data that was in
 * the URL which "account" was parsed from.  That is, user and
 * password won't be assigned unless they were in the URL too.
 *
 * However, for header and body cache, we always want to include the username
 * to prevent cross-muttrc name collisions.  For that case, pass 1 to
 * force_users
 */
void mutt_account_tourl(ACCOUNT *account, ciss_url_t *url, int force_user)
{
  url->scheme = U_UNKNOWN;
  url->user = NULL;
  url->pass = NULL;
  url->port = 0;

#ifdef USE_IMAP
  if (account->type == MUTT_ACCT_TYPE_IMAP)
  {
    if (account->flags & MUTT_ACCT_SSL)
      url->scheme = U_IMAPS;
    else
      url->scheme = U_IMAP;
  }
#endif

#ifdef USE_POP
  if (account->type == MUTT_ACCT_TYPE_POP)
  {
    if (account->flags & MUTT_ACCT_SSL)
      url->scheme = U_POPS;
    else
      url->scheme = U_POP;
  }
#endif

#ifdef USE_SMTP
  if (account->type == MUTT_ACCT_TYPE_SMTP)
  {
    if (account->flags & MUTT_ACCT_SSL)
      url->scheme = U_SMTPS;
    else
      url->scheme = U_SMTP;
  }
#endif

  url->host = account->host;
  if (account->flags & MUTT_ACCT_PORT)
    url->port = account->port;
  if (account->flags & MUTT_ACCT_USER)
  {
    if (force_user || (account->flags & MUTT_ACCT_USER_FROM_URL))
      url->user = account->user;
  }
  if ((account->flags & MUTT_ACCT_PASS) &&
      (account->flags & MUTT_ACCT_PASS_FROM_URL))
  {
    url->pass = account->pass;
  }
}

/* mutt_account_getuser: retrieve username into ACCOUNT, if necessary */
int mutt_account_getuser(ACCOUNT *account)
{
  char prompt[SHORT_STRING];
  char userbuf[LONG_STRING];

  /* already set */
  if (account->flags & MUTT_ACCT_USER)
    return 0;
#ifdef USE_IMAP
  else if ((account->type == MUTT_ACCT_TYPE_IMAP) && ImapUser)
    mutt_str_replace(&account->user, ImapUser);
#endif
#ifdef USE_POP
  else if ((account->type == MUTT_ACCT_TYPE_POP) && PopUser)
    mutt_str_replace(&account->user, PopUser);
#endif
  else if (option(OPTNOCURSES))
    return -1;
  /* prompt (defaults to unix username), copy into account->user */
  else
  {
    snprintf(prompt, sizeof(prompt), _("Username at %s: "), account->host);
    strfcpy(userbuf, NONULL(Username), sizeof(userbuf));
    if (mutt_get_field_unbuffered(prompt, userbuf, sizeof(userbuf), 0))
      return -1;
    mutt_str_replace(&account->user, userbuf);
  }

  account->flags |= MUTT_ACCT_USER;

  return 0;
}

int mutt_account_getlogin(ACCOUNT *account)
{
  /* already set */
  if (account->flags & MUTT_ACCT_LOGIN)
    return 0;
#ifdef USE_IMAP
  else if (account->type == MUTT_ACCT_TYPE_IMAP)
  {
    if (ImapLogin)
    {
      mutt_str_replace(&account->login, ImapLogin);
      account->flags |= MUTT_ACCT_LOGIN;
    }
  }
#endif

  if (!(account->flags & MUTT_ACCT_LOGIN))
  {
    mutt_account_getuser(account);
    mutt_str_replace(&account->login, account->user);
  }

  account->flags |= MUTT_ACCT_LOGIN;

  return 0;
}

static void getpass_prompt(char *prompt, size_t prompt_size, ACCOUNT *account)
{
  /* L10N:
     Prompt for an account password when connecting.
     %s@%s is user@host
  */
  snprintf(prompt, prompt_size, _("Password for %s@%s: "),
           account->flags & MUTT_ACCT_LOGIN ? account->login : account->user,
           account->host);
}

int _mutt_account_getpass(ACCOUNT *account,
                          void (*prompt_func)(char *, size_t, ACCOUNT *))
{
  char prompt[SHORT_STRING];
  char passbuf[LONG_STRING];

  if (account->flags & MUTT_ACCT_PASS)
    return 0;
#ifdef USE_IMAP
  else if ((account->type == MUTT_ACCT_TYPE_IMAP) && ImapPass)
    mutt_str_replace(&account->pass, ImapPass);
#endif
#ifdef USE_POP
  else if ((account->type == MUTT_ACCT_TYPE_POP) && PopPass)
    mutt_str_replace(&account->pass, PopPass);
#endif
#ifdef USE_SMTP
  else if ((account->type == MUTT_ACCT_TYPE_SMTP) && SmtpPass)
    mutt_str_replace(&account->pass, SmtpPass);
#endif
  else if (option(OPTNOCURSES))
    return -1;
  else
  {
    prompt_func(prompt, sizeof(prompt), account);
    passbuf[0] = '\0';
    if (mutt_get_password(prompt, passbuf, sizeof(passbuf)))
      return -1;
    mutt_str_replace(&account->pass, passbuf);
  }

  account->flags |= MUTT_ACCT_PASS;

  return 0;
}

/* mutt_account_getpass: fetch password into ACCOUNT, if necessary */
int mutt_account_getpass(ACCOUNT *account)
{
  return _mutt_account_getpass(account, getpass_prompt);
}

void mutt_account_unsetpass(ACCOUNT *account)
{
  account->flags &= ~MUTT_ACCT_PASS;
  account->flags &= ~MUTT_ACCT_PASS_FROM_URL;
}

/* mutt_account_getoauthbearer: call external command to generate the
 * oauth refresh token for this ACCOUNT, then create and encode the
 * OAUTHBEARER token based on RFC 7628.
 *
 * Returns 0 on success, -1 on failure.
 *
 * If xoauth2 is set, a deprecated XOAUTH2 token will be generated instead.
 */
int mutt_account_getoauthbearer(ACCOUNT *account, BUFFER *authbearer, int xoauth2)
{
  FILE  *fp;
  char *cmd = NULL;
  char *token = NULL;
  size_t token_size = 0;
  pid_t pid;
  BUFFER *unencoded_bearertoken = NULL;

  mutt_buffer_clear(authbearer);

  /* The oauthbearer token includes the login */
  if (mutt_account_getlogin(account))
    return -1;

#ifdef USE_IMAP
  if ((account->type == MUTT_ACCT_TYPE_IMAP) && ImapOauthRefreshCmd)
    cmd = ImapOauthRefreshCmd;
#endif
#ifdef USE_POP
  else if ((account->type == MUTT_ACCT_TYPE_POP) && PopOauthRefreshCmd)
    cmd = PopOauthRefreshCmd;
#endif
#ifdef USE_SMTP
  else if ((account->type == MUTT_ACCT_TYPE_SMTP) && SmtpOauthRefreshCmd)
    cmd = SmtpOauthRefreshCmd;
#endif

  if (cmd == NULL)
  {
    /* L10N: You will see this error message if (1) you have "oauthbearer" in
       one of your $*_authenticators and (2) you do not have the corresponding
       $*_oauth_refresh_command defined. So the message does not mean "None of
       your $*_oauth_refresh_command's are defined."
    */
    mutt_error(_("mutt_account_getoauthbearer: No OAUTH refresh command defined"));
    return -1;
  }

  if ((pid = mutt_create_filter(cmd, NULL, &fp, NULL)) < 0)
  {
    mutt_perror _("mutt_account_getoauthbearer: Unable to run refresh command");
    return -1;
  }

  /* read line */
  token = mutt_read_line(NULL, &token_size, fp, NULL, 0);
  safe_fclose(&fp);
  mutt_wait_filter(pid);

  /* The refresh cmd in some cases will invoke gpg to decrypt a token */
  if (!option(OPTNOCURSES))
    mutt_need_hard_redraw();

  if (token == NULL || *token == '\0')
  {
    mutt_error(_("mutt_account_getoauthbearer: Command returned empty string"));
    FREE(&token);
    return -1;
  }

  unencoded_bearertoken = mutt_buffer_pool_get();

  if (xoauth2)
    mutt_buffer_printf(unencoded_bearertoken,
                       "user=%s\001auth=Bearer %s\001\001",
                       account->login, token);
  else
    mutt_buffer_printf(unencoded_bearertoken,
                       "n,a=%s,\001host=%s\001port=%d\001auth=Bearer %s\001\001",
                       account->login, account->host, account->port, token);

  FREE(&token);

  mutt_buffer_to_base64(authbearer,
                        (const unsigned char *) mutt_b2s(unencoded_bearertoken),
                        mutt_buffer_len(unencoded_bearertoken));

  mutt_buffer_pool_release(&unencoded_bearertoken);

  return 0;
}
