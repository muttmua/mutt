/*
 *     This program is free software; you can redistribute it
 *     and/or modify it under the terms of the GNU General Public
 *     License as published by the Free Software Foundation; either
 *     version 2 of the License, or (at your option) any later
 *     version.
 *
 *     This program is distributed in the hope that it will be
 *     useful, but WITHOUT ANY WARRANTY; without even the implied
 *     warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *     PURPOSE.  See the GNU General Public License for more
 *     details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *     Boston, MA  02110-1301, USA.
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "mutt_curses.h"
#include "mutt_menu.h"
#include "url.h"

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>

#include <locale.h>

static const struct mapping_t ListHelp[] = {
  { N_("Exit"),        OP_EXIT },
  { N_("Reply"),       OP_LIST_REPLY },
  { N_("Post"),        OP_LIST_POST },
  { N_("Subscribe"),   OP_LIST_SUBSCRIBE },
  { N_("Unsubscribe"), OP_LIST_UNSUBSCRIBE },
  { N_("Owner"),       OP_LIST_OWNER },
  { NULL,              0 }
};

static const struct mapping_t ListActions[] = {
  /* L10N: localized names of RFC 2369 list operations */
  { N_("Help"),        offsetof(ENVELOPE, list_help) },
  { N_("Post"),        offsetof(ENVELOPE, list_post) },
  { N_("Subscribe"),   offsetof(ENVELOPE, list_subscribe) },
  { N_("Unsubscribe"), offsetof(ENVELOPE, list_unsubscribe) },
  { N_("Archives"),    offsetof(ENVELOPE, list_archive) },
  { N_("Owner"),       offsetof(ENVELOPE, list_owner) },
  { NULL, 0 }
};

struct menu_data {
  HEADER *msg;
  char fmt[12];
  int num;
};

/* Computes a printf() style format string including enough
 * field width for the largest list action name. */
static void make_field_format (int max, char *dst)
{
  int len = 0;
  struct mapping_t *action;

  dst[--max] = '\0';
  for (action = (struct mapping_t *)ListActions; action->name; action++)
    len = MAX(len, mutt_strwidth (_(action->name)));

  /* n.b. not localized - is a metaformat */
  snprintf(dst, max, "%%%dl: %%v", len);
}

static const char *list_format_str (char *dest, size_t destlen, size_t col,
                                    int cols, char op, const char *src,
                                    const char *fmt, const char *ifstring,
                                    const char *elsestring,
                                    void *data, format_flag flags)
{
  struct menu_data *md = (struct menu_data *) data;
  char **value;

  value = (char **)( ((caddr_t) md->msg->env) + ListActions[md->num].value);

  switch (op)
  {
    case 'l':
      mutt_format_s (dest, destlen, fmt, _(ListActions[md->num].name));
      break;
    case 'v':
      mutt_format_s (dest, destlen, fmt, *value ? *value : "--");
      break;
  }

  return (src);
}

static void make_entry (char *b, size_t blen, MUTTMENU *menu, int num)
{
  struct menu_data *md = menu->data;

  md->num = num;
  mutt_FormatString (b, blen, 0, MuttIndexWindow->cols,
                     md->fmt, list_format_str, md,
		     MUTT_FORMAT_ARROWCURSOR);
}

static int list_action (CONTEXT *ctx, HEADER *msg, struct mapping_t *action)
{
  HEADER *newmsg = NULL;
  char *body = NULL;
  char **address = NULL;

  address = (char **)( ((caddr_t) msg->env) + action->value);
  if (address == NULL || *address == NULL)
  {
    /* L10N: given when an rfc 2369 action is not specified by this message */
    mutt_error (_("No list action available for %s."), action->name);
    return 0;
  }

  if (url_check_scheme (*address) != U_MAILTO)
  {
    /* L10N: given when a message's rfc 2369 action is not mailto: */
    mutt_error (_("List actions only support mailto: URIs. (Try a browser?)"));
    return 1;
  }

  newmsg = mutt_new_header ();
  newmsg->env = mutt_new_envelope ();
  if (url_parse_mailto (newmsg->env, &body, *address) < 0)
  {
    /* L10N: given when mailto: URI was unparsable while trying to execute it */
    mutt_error (_("Could not parse mailto: URI."));
    return 1;
  }

  mutt_send_message (SENDBACKGROUNDEDIT, newmsg, NULL, ctx, NULL);
  return 1;
}

void mutt_list_menu (CONTEXT *ctx, HEADER *msg)
{
  MUTTMENU *menu = NULL;
  int done = 0;
  char helpstr[LONG_STRING];
  struct menu_data mdata = {0};

  mdata.msg = msg;
  make_field_format(sizeof(mdata.fmt), mdata.fmt);

  menu = mutt_new_menu (MENU_LIST);
  menu->max = (sizeof(ListActions) / sizeof(struct mapping_t)) - 1;
  menu->make_entry = make_entry;
  menu->data = &mdata;
  /* L10N: menu name for list actions */
  menu->title = _("Available mailing list actions");
  menu->help = mutt_compile_help (helpstr, sizeof (helpstr), MENU_LIST, ListHelp);
  mutt_push_current_menu (menu);

  while (!done)
  {
    switch (mutt_menuLoop (menu))
    {

    case OP_LIST_HELP:
      done = list_action(ctx, msg, (struct mapping_t *)&ListActions[0]);
      break;

    case OP_LIST_POST:
      done = list_action(ctx, msg, (struct mapping_t *)&ListActions[1]);
      break;

    case OP_LIST_SUBSCRIBE:
      done = list_action(ctx, msg, (struct mapping_t *)&ListActions[2]);
      break;

    case OP_LIST_UNSUBSCRIBE:
      done = list_action(ctx, msg, (struct mapping_t *)&ListActions[3]);
      break;

    case OP_LIST_ARCHIVE:
      done = list_action(ctx, msg, (struct mapping_t *)&ListActions[4]);
      break;

    case OP_LIST_OWNER:
      done = list_action(ctx, msg, (struct mapping_t *)&ListActions[5]);
      break;

    case OP_GENERIC_SELECT_ENTRY:
      done = list_action(ctx, msg, (struct mapping_t *)&ListActions[menu->current]);
      break;

    case OP_EXIT:
      done = 1;
      break;
    }
  }

  mutt_pop_current_menu (menu);
  mutt_menuDestroy (&menu);
  return;
}
