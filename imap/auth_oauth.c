/*
 * Copyright (C) 1999-2001,2005 Brendan Cully <brendan@kublai.com>
 * Copyright (C) 2018 Brandon Long <blong@fiction.net>
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

/* IMAP login/authentication code */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "imap_private.h"
#include "auth.h"

/* imap_auth_oauth: AUTH=OAUTHBEARER support. See RFC 7628 */
imap_auth_res_t imap_auth_oauth (IMAP_DATA* idata, const char* method)
{
  char* ibuf = NULL;
  char* oauth_buf = NULL;
  int len, ilen, oalen;
  int rc;

  /* For now, we only support SASL_IR also and over TLS */
  if (!mutt_bit_isset (idata->capabilities, AUTH_OAUTHBEARER) ||
      !mutt_bit_isset (idata->capabilities, SASL_IR) ||
      !idata->conn->ssf)
    return IMAP_AUTH_UNAVAIL;

  mutt_message _("Authenticating (OAUTHBEARER)...");

  /* get auth info */
  if (mutt_account_getlogin (&idata->conn->account))
    return IMAP_AUTH_FAILURE;

  /* We get the access token from the "imap_pass" field */
  if (mutt_account_getpass (&idata->conn->account))
    return IMAP_AUTH_FAILURE;

  /* Determine the length of the keyed message digest, add 50 for
   * overhead.
   */
  oalen = strlen (idata->conn->account.user) +
    strlen (idata->conn->account.host) + 
    strlen (idata->conn->account.pass) + 50; 
  oauth_buf = safe_malloc (oalen);

  snprintf (oauth_buf, oalen,
    "n,a=%s,\001host=%s\001port=%d\001auth=Bearer %s\001\001",
    idata->conn->account.user, idata->conn->account.host,
    idata->conn->account.port, idata->conn->account.pass);

  /* ibuf must be long enough to store the base64 encoding of
   * oauth_buf, plus the additional debris.
   */

  ilen = strlen (oauth_buf) * 2 + 30;
  ibuf = safe_malloc (ilen);
  ibuf[0] = '\0';

  safe_strcat (ibuf, ilen, "AUTHENTICATE OAUTHBEARER ");
  len = strlen(ibuf);
  
  mutt_to_base64 ((unsigned char*) (ibuf + len),
                  (unsigned char*) oauth_buf, strlen (oauth_buf),
		  ilen - len);

  /* This doesn't really contain a password, but the token is good for
   * an hour, so suppress it anyways.
   */
  rc = imap_exec (idata, ibuf, IMAP_CMD_FAIL_OK | IMAP_CMD_PASS);

  FREE (&oauth_buf);
  FREE (&ibuf);
  
  if (!rc)
  {
    mutt_clear_error();
    return IMAP_AUTH_SUCCESS;
  }

  /* The error response was in SASL continuation, so "continue" the SASL
   * to cause a failure and exit SASL input.
   */
  mutt_socket_write (idata->conn, "an noop\r\n");

  mutt_error _("OAUTHBEARER authentication failed.");
  mutt_sleep (2);
  return IMAP_AUTH_FAILURE;
}
