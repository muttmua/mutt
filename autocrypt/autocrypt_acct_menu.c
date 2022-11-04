/*
 * Copyright (C) 2019 Kevin J. McCarthy <kevin@8t8.us>
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
#include "mutt_menu.h"
#include "mutt_idna.h"

#include "autocrypt.h"
#include "autocrypt_private.h"

typedef struct entry
{
  int tagged;    /* TODO */
  int num;
  AUTOCRYPT_ACCOUNT *account;
  ADDRESS *addr;
} ENTRY;

static const struct mapping_t AutocryptAcctHelp[] = {
  { N_("Exit"),   OP_EXIT },
  /* L10N: Autocrypt Account Menu Help line:
     create new account
  */
  { N_("Create"),   OP_AUTOCRYPT_CREATE_ACCT },
  /* L10N: Autocrypt Account Menu Help line:
     delete account
  */
  { N_("Delete"),   OP_AUTOCRYPT_DELETE_ACCT },
  /* L10N: Autocrypt Account Menu Help line:
     toggle an account active/inactive
     The words here are abbreviated to keep the help line compact.
     It currently has the content:
     q:Exit  c:Create  D:Delete  a:Tgl Active  p:Prf Encr  ?:Help
  */
  { N_("Tgl Active"),  OP_AUTOCRYPT_TOGGLE_ACTIVE },
  /* L10N: Autocrypt Account Menu Help line:
     toggle "prefer-encrypt" on an account
     The words here are abbreviated to keep the help line compact.
     It currently has the content:
     q:Exit  c:Create  D:Delete  a:Tgl Active  p:Prf Encr  ?:Help
  */
  { N_("Prf Encr"), OP_AUTOCRYPT_TOGGLE_PREFER },
  { N_("Help"),   OP_HELP },
  { NULL,	  0 }
};

static const char *account_format_str (char *dest, size_t destlen, size_t col,
                                       int cols, char op, const char *src,
                                       const char *fmt, const char *ifstring,
                                       const char *elsestring,
                                       void *data, format_flag flags)
{
  ENTRY *entry = (ENTRY *)data;
  char tmp[SHORT_STRING];

  switch (op)
  {
    case 'a':
      mutt_format_s (dest, destlen, fmt, entry->addr->mailbox);
      break;
    case 'k':
      mutt_format_s (dest, destlen, fmt, entry->account->keyid);
      break;
    case 'n':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, entry->num);
      break;
    case 'p':
      if (entry->account->prefer_encrypt)
        /* L10N:
           Autocrypt Account menu.
           flag that an account has prefer-encrypt set
        */
        mutt_format_s (dest, destlen, fmt, _("prefer encrypt"));
      else
        /* L10N:
           Autocrypt Account menu.
           flag that an account has prefer-encrypt unset;
           thus encryption will need to be manually enabled.
        */
        mutt_format_s (dest, destlen, fmt, _("manual encrypt"));
      break;
    case 's':
      if (entry->account->enabled)
        /* L10N:
           Autocrypt Account menu.
           flag that an account is enabled/active
        */
        mutt_format_s (dest, destlen, fmt, _("active"));
      else
        /* L10N:
           Autocrypt Account menu.
           flag that an account is disabled/inactive
        */
        mutt_format_s (dest, destlen, fmt, _("inactive"));
      break;
  }

  return (src);
}

static void account_entry (char *s, size_t slen, MUTTMENU *m, int num)
{
  ENTRY *entry = &((ENTRY *) m->data)[num];

  mutt_FormatString (s, slen, 0, MuttIndexWindow->cols,
                     NONULL (AutocryptAcctFormat), account_format_str,
		     entry, MUTT_FORMAT_ARROWCURSOR);
}

static MUTTMENU *create_menu (void)
{
  MUTTMENU *menu = NULL;
  AUTOCRYPT_ACCOUNT **accounts = NULL;
  ENTRY *entries = NULL;
  int num_accounts = 0, i;
  char *helpstr;

  if (mutt_autocrypt_db_account_get_all (&accounts, &num_accounts) < 0)
    return NULL;

  menu = mutt_new_menu (MENU_AUTOCRYPT_ACCT);
  menu->make_entry = account_entry;
  /* menu->tag = account_tag; */
  /* L10N:
     Autocrypt Account Management Menu title
  */
  menu->title = _("Autocrypt Accounts");
  helpstr = safe_malloc (STRING);
  menu->help = mutt_compile_help (helpstr, STRING, MENU_AUTOCRYPT_ACCT,
                                  AutocryptAcctHelp);

  menu->data = entries = safe_calloc (num_accounts, sizeof(ENTRY));
  menu->max = num_accounts;

  for (i = 0; i < num_accounts; i++)
  {
    entries[i].num = i + 1;
    /* note: we are transfering the account pointer to the entries
     * array, and freeing the accounts array below.  the account
     * will be freed in free_menu().
     */
    entries[i].account = accounts[i];

    entries[i].addr = rfc822_new_address ();
    entries[i].addr->mailbox = safe_strdup (accounts[i]->email_addr);
    mutt_addrlist_to_local (entries[i].addr);
  }
  FREE (&accounts);

  mutt_push_current_menu (menu);

  return menu;
}

static void free_menu (MUTTMENU **menu)
{
  int i;
  ENTRY *entries;

  entries = (ENTRY *)(*menu)->data;
  for (i = 0; i < (*menu)->max; i++)
  {
    mutt_autocrypt_db_account_free (&entries[i].account);
    rfc822_free_address (&entries[i].addr);
  }
  FREE (&(*menu)->data);

  mutt_pop_current_menu (*menu);
  FREE (&(*menu)->help);
  mutt_menuDestroy (menu);
}

static void toggle_active (ENTRY *entry)
{
  entry->account->enabled = !entry->account->enabled;
  if (mutt_autocrypt_db_account_update (entry->account) != 0)
  {
    entry->account->enabled = !entry->account->enabled;
    /* L10N:
       This error message is displayed if a database update of an
       account record fails for some odd reason.
    */
    mutt_error _("Error updating account record");
  }
}

static void toggle_prefer_encrypt (ENTRY *entry)
{
  entry->account->prefer_encrypt = !entry->account->prefer_encrypt;
  if (mutt_autocrypt_db_account_update (entry->account))
  {
    entry->account->prefer_encrypt = !entry->account->prefer_encrypt;
    mutt_error _("Error updating account record");
  }
}

void mutt_autocrypt_account_menu (void)
{
  MUTTMENU *menu;
  int done = 0, op;
  ENTRY *entry;
  char msg[SHORT_STRING];

  if (!option (OPTAUTOCRYPT))
    return;

  if (mutt_autocrypt_init (0))
    return;

  menu = create_menu ();
  if (!menu)
    return;

  while (!done)
  {
    switch ((op = mutt_menuLoop (menu)))
    {
      case OP_EXIT:
        done = 1;
        break;

      case OP_AUTOCRYPT_CREATE_ACCT:
        if (!mutt_autocrypt_account_init (0))
        {
          free_menu (&menu);
          menu = create_menu ();
        }
        break;

      case OP_AUTOCRYPT_DELETE_ACCT:
        if (menu->data)
        {
          entry = (ENTRY *)(menu->data) + menu->current;
          snprintf (msg, sizeof(msg),
                    /* L10N:
                       Confirmation message when deleting an autocrypt account
                    */
                    _("Really delete account \"%s\"?"),
                    entry->addr->mailbox);
	  if (mutt_yesorno (msg, MUTT_NO) != MUTT_YES)
            break;

          if (!mutt_autocrypt_db_account_delete (entry->account))
          {
            free_menu (&menu);
            menu = create_menu ();
          }
        }
        break;

      case OP_AUTOCRYPT_TOGGLE_ACTIVE:
        if (menu->data)
        {
          entry = (ENTRY *)(menu->data) + menu->current;
          toggle_active (entry);
          menu->redraw |= REDRAW_FULL;
        }
        break;

      case OP_AUTOCRYPT_TOGGLE_PREFER:
        if (menu->data)
        {
          entry = (ENTRY *)(menu->data) + menu->current;
          toggle_prefer_encrypt (entry);
          menu->redraw |= REDRAW_FULL;
        }
        break;
    }
  }

  free_menu (&menu);
}
