/*
 * Copyright (C) 2021 Kevin J. McCarthy <kevin@8t8.us>
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
#include "mutt_sasl_gnu.h"
#include "imap_private.h"
#include "auth.h"

#include <gsasl.h>

imap_auth_res_t imap_auth_gsasl (IMAP_DATA* idata, const char* method)
{
  Gsasl_session *gsasl_session = NULL;
  const char *chosen_mech;
  BUFFER *output_buf = NULL;
  char *gsasl_step_output = NULL, *imap_step_output;
  int rc = IMAP_AUTH_FAILURE;
  int gsasl_rc = GSASL_OK, imap_step_rc = IMAP_CMD_CONTINUE;

  chosen_mech = mutt_gsasl_get_mech (method, idata->capstr);
  if (!chosen_mech)
  {
    dprint (2, (debugfile, "mutt_gsasl_get_mech() returned no usable mech\n"));
    return IMAP_AUTH_UNAVAIL;
  }

  dprint (2, (debugfile, "imap_auth_gsasl: using mech %s\n", chosen_mech));

  if (mutt_gsasl_client_new (idata->conn, chosen_mech, &gsasl_session) < 0)
  {
    dprint (1, (debugfile,
                "imap_auth_gsasl: Error allocating GSASL connection.\n"));
    return IMAP_AUTH_UNAVAIL;
  }

  mutt_message (_("Authenticating (%s)..."), chosen_mech);

  output_buf = mutt_buffer_pool_get ();
  mutt_buffer_printf (output_buf, "AUTHENTICATE %s", chosen_mech);
  if (mutt_bit_isset (idata->capabilities, SASL_IR))
  {
    gsasl_rc = gsasl_step64 (gsasl_session, "", &gsasl_step_output);
    if (gsasl_rc != GSASL_NEEDS_MORE && gsasl_rc != GSASL_OK)
    {
      dprint (1, (debugfile, "gsasl_step64() failed (%d): %s\n", gsasl_rc,
                  gsasl_strerror (gsasl_rc)));
      rc = IMAP_AUTH_UNAVAIL;
      goto bail;
    }

    mutt_buffer_addch (output_buf, ' ');
    mutt_buffer_addstr (output_buf, gsasl_step_output);
    gsasl_free (gsasl_step_output);
  }
  imap_cmd_start (idata, mutt_b2s (output_buf));

  do
  {
    do
      imap_step_rc = imap_cmd_step (idata);
    while (imap_step_rc == IMAP_CMD_CONTINUE);
    if (imap_step_rc == IMAP_CMD_BAD || imap_step_rc == IMAP_CMD_NO)
      goto bail;

    if (imap_step_rc != IMAP_CMD_RESPOND)
      break;

    imap_step_output = imap_next_word (idata->buf);

    gsasl_rc = gsasl_step64 (gsasl_session, imap_step_output,
                             &gsasl_step_output);
    if (gsasl_rc == GSASL_NEEDS_MORE || gsasl_rc == GSASL_OK)
    {
      mutt_buffer_strcpy (output_buf, gsasl_step_output);
      gsasl_free (gsasl_step_output);
    }
    /* if a sasl error occured, send an abort string */
    else
    {
      dprint (1, (debugfile, "gsasl_step64() failed (%d): %s\n", gsasl_rc,
                  gsasl_strerror (gsasl_rc)));
      mutt_buffer_strcpy(output_buf, "*");
    }

    mutt_buffer_addstr (output_buf, "\r\n");
    mutt_socket_write (idata->conn, mutt_b2s (output_buf));
  }
  while (gsasl_rc == GSASL_NEEDS_MORE || gsasl_rc == GSASL_OK);

  if (imap_step_rc != IMAP_CMD_OK)
  {
    do
      imap_step_rc = imap_cmd_step (idata);
    while (imap_step_rc == IMAP_CMD_CONTINUE);
  }

  if (imap_step_rc == IMAP_CMD_RESPOND)
  {
    mutt_socket_write (idata->conn, "*\r\n");
    goto bail;
  }

  if ((gsasl_rc != GSASL_OK) || (imap_step_rc != IMAP_CMD_OK))
    goto bail;

  if (imap_code (idata->buf))
    rc = IMAP_AUTH_SUCCESS;

bail:
  mutt_buffer_pool_release (&output_buf);
  mutt_gsasl_client_finish (&gsasl_session);

  if (rc == IMAP_AUTH_FAILURE)
  {
    dprint (2, (debugfile, "imap_auth_gsasl: %s failed\n", chosen_mech));
    mutt_error _("SASL authentication failed.");
    mutt_sleep (2);
  }

  return rc;
}
