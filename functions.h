/*
 * Copyright (C) 1996-2000,2002 Michael R. Elkins <me@mutt.org>
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

/*
 * This file contains the structures needed to parse ``bind'' commands, as
 * well as the default bindings for each menu.
 *
 * Notes:
 *
 * - For "enter" bindings, add entries for "\n" and "\r" and
 *   "<keypadenter>".
 *
 * - If you need to bind a control char, use the octal value because the \cX
 * construct does not work at this level.
 *
 * - The magic "map:" comments define how the map will be called in the
 * manual. Lines starting with "**" will be included in the manual.
 *
 */

#ifdef _MAKEDOC
# include "config.h"
# include "doc/makedoc-defs.h"
#endif

const struct menu_func_op_t OpGeneric[] = { /* map: generic */
  /*
  ** <para>
  ** The <emphasis>generic</emphasis> menu is not a real menu, but specifies common functions
  ** (such as movement) available in all menus except for <emphasis>pager</emphasis> and
  ** <emphasis>editor</emphasis>.  Changing settings for this menu will affect the default
  ** bindings for all menus (except as noted).
  ** </para>
  */
  { "bottom-page",     OP_BOTTOM_PAGE },
  { "check-stats",     OP_CHECK_STATS },
  { "current-bottom",  OP_CURRENT_BOTTOM },
  { "current-middle",  OP_CURRENT_MIDDLE },
  { "current-top",     OP_CURRENT_TOP },
  { "end-cond",        OP_END_COND },
  { "enter-command",   OP_ENTER_COMMAND },
  { "error-history",   OP_ERROR_HISTORY },
  { "exit",            OP_EXIT },
  { "first-entry",     OP_FIRST_ENTRY },
  { "half-down",       OP_HALF_DOWN },
  { "half-up",         OP_HALF_UP },
  { "help",            OP_HELP },
  { "jump",            OP_JUMP },
  { "last-entry",      OP_LAST_ENTRY },
  { "middle-page",     OP_MIDDLE_PAGE },
  { "next-entry",      OP_NEXT_ENTRY },
  { "next-line",       OP_NEXT_LINE },
  { "next-page",       OP_NEXT_PAGE },
  { "previous-entry",  OP_PREV_ENTRY },
  { "previous-line",   OP_PREV_LINE },
  { "previous-page",   OP_PREV_PAGE },
  { "refresh",         OP_REDRAW },
  { "search",          OP_SEARCH },
  { "search-next",     OP_SEARCH_NEXT },
  { "search-opposite", OP_SEARCH_OPPOSITE },
  { "search-reverse",  OP_SEARCH_REVERSE },
  { "select-entry",    OP_GENERIC_SELECT_ENTRY },
  { "shell-escape",    OP_SHELL_ESCAPE },
  { "tag-entry",       OP_TAG },
  { "tag-prefix",      OP_TAG_PREFIX },
  { "tag-prefix-cond", OP_TAG_PREFIX_COND },
  { "top-page",        OP_TOP_PAGE },
  { "what-key",        OP_WHAT_KEY },
  { NULL,              0 }
};

const struct menu_op_seq_t GenericDefaultBindings[] = { /* map: generic */
  { OP_BOTTOM_PAGE,             "L" },
  { OP_ENTER_COMMAND,           ":" },
  { OP_EXIT,                    "q" },
  { OP_FIRST_ENTRY,             "<home>" },
  { OP_FIRST_ENTRY,             "=" },
  { OP_GENERIC_SELECT_ENTRY,    "<keypadenter>" },
  { OP_GENERIC_SELECT_ENTRY,    "\n" },
  { OP_GENERIC_SELECT_ENTRY,    "\r" },
  { OP_HALF_DOWN,               "]" },
  { OP_HALF_UP,                 "[" },
  { OP_HELP,                    "?" },
  { OP_JUMP,                    "1" },
  { OP_JUMP,                    "2" },
  { OP_JUMP,                    "3" },
  { OP_JUMP,                    "4" },
  { OP_JUMP,                    "5" },
  { OP_JUMP,                    "6" },
  { OP_JUMP,                    "7" },
  { OP_JUMP,                    "8" },
  { OP_JUMP,                    "9" },
  { OP_LAST_ENTRY,              "*" },
  { OP_LAST_ENTRY,              "<end>" },
  { OP_MIDDLE_PAGE,             "M" },
  { OP_NEXT_ENTRY,              "<down>" },
  { OP_NEXT_ENTRY,              "j" },
  { OP_NEXT_LINE,               ">" },
  { OP_NEXT_PAGE,               "<pagedown>" },
  { OP_NEXT_PAGE,               "<right>" },
  { OP_NEXT_PAGE,               "z" },
  { OP_PREV_ENTRY,              "<up>" },
  { OP_PREV_ENTRY,              "k" },
  { OP_PREV_LINE,               "<" },
  { OP_PREV_PAGE,               "<left>" },
  { OP_PREV_PAGE,               "<pageup>" },
  { OP_PREV_PAGE,               "Z" },
  { OP_REDRAW,                  "\014" },
  { OP_SEARCH,                  "/" },
  { OP_SEARCH_NEXT,             "n" },
  { OP_SEARCH_REVERSE,          "\033/" },
  { OP_SHELL_ESCAPE,            "!" },
  { OP_TAG,                     "t" },
  { OP_TAG_PREFIX,              ";" },
  { OP_TOP_PAGE,                "H" },
  { 0,                  NULL }
};

const struct menu_func_op_t OpMain[] = { /* map: index */
#ifdef USE_AUTOCRYPT
  { "autocrypt-acct-menu",       OP_AUTOCRYPT_ACCT_MENU },
#endif
  { "background-compose-menu",   OP_BACKGROUND_COMPOSE_MENU },
  { "bounce-message",            OP_BOUNCE_MESSAGE },
  { "break-thread",              OP_MAIN_BREAK_THREAD },
  { "browse-mailboxes",          OP_MAIN_BROWSE_MAILBOXES },
  { "browse-mailboxes-readonly", OP_MAIN_BROWSE_MAILBOXES_READONLY },
  { "buffy-list",                OP_BUFFY_LIST },
  { "change-folder",             OP_MAIN_CHANGE_FOLDER },
  { "change-folder-readonly",    OP_MAIN_CHANGE_FOLDER_READONLY },
  { "check-traditional-pgp",     OP_CHECK_TRADITIONAL },
  { "clear-flag",                OP_MAIN_CLEAR_FLAG },
  { "collapse-all",              OP_MAIN_COLLAPSE_ALL },
  { "collapse-thread",           OP_MAIN_COLLAPSE_THREAD },
  { "compose-to-sender",         OP_COMPOSE_TO_SENDER },
  { "copy-message",              OP_COPY_MESSAGE },
  { "create-alias",              OP_CREATE_ALIAS },
  { "decode-copy",               OP_DECODE_COPY },
  { "decode-save",               OP_DECODE_SAVE },
  { "decrypt-copy",              OP_DECRYPT_COPY },
  { "decrypt-save",              OP_DECRYPT_SAVE },
  { "delete-message",            OP_DELETE },
  { "delete-pattern",            OP_MAIN_DELETE_PATTERN },
  { "delete-subthread",          OP_DELETE_SUBTHREAD },
  { "delete-thread",             OP_DELETE_THREAD },
  { "display-address",           OP_DISPLAY_ADDRESS },
  { "display-message",           OP_DISPLAY_MESSAGE },
  { "display-toggle-weed",       OP_DISPLAY_HEADERS },
  { "edit",                      OP_EDIT_MESSAGE },
  { "edit-label",                OP_EDIT_LABEL },
  { "edit-type",                 OP_EDIT_TYPE },
  { "extract-keys",              OP_EXTRACT_KEYS },
#ifdef USE_POP
  { "fetch-mail",                OP_MAIN_FETCH_MAIL },
#endif
  { "flag-message",              OP_FLAG_MESSAGE },
  { "forget-passphrase",         OP_FORGET_PASSPHRASE },
  { "forward-message",           OP_FORWARD_MESSAGE },
  { "group-chat-reply",          OP_GROUP_CHAT_REPLY },
  { "group-reply",               OP_GROUP_REPLY },
#ifdef USE_IMAP
  { "imap-fetch-mail",           OP_MAIN_IMAP_FETCH },
  { "imap-logout-all",           OP_MAIN_IMAP_LOGOUT_ALL },
#endif
  { "limit",                     OP_MAIN_LIMIT },
  { "link-threads",              OP_MAIN_LINK_THREADS },
  { "list-action",               OP_LIST_ACTION },
  { "list-reply",                OP_LIST_REPLY },
  { "mail",                      OP_MAIL },
  { "mail-key",                  OP_MAIL_KEY },
  { "mark-message",              OP_MARK_MSG },
  { "next-new",                  OP_MAIN_NEXT_NEW },
  { "next-new-then-unread",      OP_MAIN_NEXT_NEW_THEN_UNREAD },
  { "next-subthread",            OP_MAIN_NEXT_SUBTHREAD },
  { "next-thread",               OP_MAIN_NEXT_THREAD },
  { "next-undeleted",            OP_MAIN_NEXT_UNDELETED },
  { "next-unread",               OP_MAIN_NEXT_UNREAD },
  { "next-unread-mailbox",       OP_MAIN_NEXT_UNREAD_MAILBOX },
  { "parent-message",            OP_MAIN_PARENT_MESSAGE },
  { "pipe-message",              OP_PIPE },
  { "previous-new",              OP_MAIN_PREV_NEW },
  { "previous-new-then-unread",  OP_MAIN_PREV_NEW_THEN_UNREAD },
  { "previous-subthread",        OP_MAIN_PREV_SUBTHREAD },
  { "previous-thread",           OP_MAIN_PREV_THREAD },
  { "previous-undeleted",        OP_MAIN_PREV_UNDELETED },
  { "previous-unread",           OP_MAIN_PREV_UNREAD },
  { "print-message",             OP_PRINT },
  { "purge-message",             OP_PURGE_MESSAGE },
  { "query",                     OP_QUERY },
  { "quit",                      OP_QUIT },
  { "read-subthread",            OP_MAIN_READ_SUBTHREAD },
  { "read-thread",               OP_MAIN_READ_THREAD },
  { "recall-message",            OP_RECALL_MESSAGE },
  { "reply",                     OP_REPLY },
  { "resend-message",            OP_RESEND },
  { "root-message",              OP_MAIN_ROOT_MESSAGE },
  { "save-message",              OP_SAVE },
  { "set-flag",                  OP_MAIN_SET_FLAG },
  { "show-limit",                OP_MAIN_SHOW_LIMIT },
  { "show-version",              OP_VERSION },
#ifdef USE_SIDEBAR
  { "sidebar-first",             OP_SIDEBAR_FIRST },
  { "sidebar-last",              OP_SIDEBAR_LAST },
  { "sidebar-next",              OP_SIDEBAR_NEXT },
  { "sidebar-next-new",          OP_SIDEBAR_NEXT_NEW },
  { "sidebar-open",              OP_SIDEBAR_OPEN },
  { "sidebar-page-down",         OP_SIDEBAR_PAGE_DOWN },
  { "sidebar-page-up",           OP_SIDEBAR_PAGE_UP },
  { "sidebar-prev",              OP_SIDEBAR_PREV },
  { "sidebar-prev-new",          OP_SIDEBAR_PREV_NEW },
  { "sidebar-toggle-visible",    OP_SIDEBAR_TOGGLE_VISIBLE },
#endif
  { "sort-mailbox",              OP_SORT },
  { "sort-reverse",              OP_SORT_REVERSE },
  { "sync-mailbox",              OP_MAIN_SYNC_FOLDER },
  { "tag-pattern",               OP_MAIN_TAG_PATTERN },
  { "tag-subthread",             OP_TAG_SUBTHREAD },
  { "tag-thread",                OP_TAG_THREAD },
  { "toggle-new",                OP_TOGGLE_NEW },
  { "toggle-write",              OP_TOGGLE_WRITE },
  { "undelete-message",          OP_UNDELETE },
  { "undelete-pattern",          OP_MAIN_UNDELETE_PATTERN },
  { "undelete-subthread",        OP_UNDELETE_SUBTHREAD },
  { "undelete-thread",           OP_UNDELETE_THREAD },
  { "untag-pattern",             OP_MAIN_UNTAG_PATTERN },
  { "view-attachments",          OP_VIEW_ATTACHMENTS },
  { NULL,                        0 }
};

const struct menu_op_seq_t MainDefaultBindings[] = { /* map: index */
#ifdef USE_AUTOCRYPT
  { OP_AUTOCRYPT_ACCT_MENU,         "A" },
#endif
  { OP_BACKGROUND_COMPOSE_MENU,     "B" },
  { OP_BOUNCE_MESSAGE,              "b" },
  { OP_BUFFY_LIST,                  "." },
  { OP_CHECK_TRADITIONAL,           "\033P" },
  { OP_COPY_MESSAGE,                "C" },
  { OP_CREATE_ALIAS,                "a" },
  { OP_DECODE_COPY,                 "\033C" },
  { OP_DECODE_SAVE,                 "\033s" },
  { OP_DELETE,                      "d" },
  { OP_DELETE_SUBTHREAD,            "\033d" },
  { OP_DELETE_THREAD,               "\004" },
  { OP_DISPLAY_ADDRESS,             "@" },
  { OP_DISPLAY_HEADERS,             "h" },
  { OP_DISPLAY_MESSAGE,             "\r" },
  { OP_DISPLAY_MESSAGE,             "\n" },
  { OP_DISPLAY_MESSAGE,             " " },
  { OP_DISPLAY_MESSAGE,             "<keypadenter>" },
  { OP_EDIT_LABEL,                  "Y" },
  { OP_EDIT_MESSAGE,                "e" },
  { OP_EDIT_TYPE,                   "\005" },
  { OP_EXIT,                        "x" },
  { OP_EXTRACT_KEYS,                "\013" },
  { OP_FLAG_MESSAGE,                "F" },
  { OP_FORGET_PASSPHRASE,           "\006" },
  { OP_FORWARD_MESSAGE,             "f" },
  { OP_GROUP_REPLY,                 "g" },
  { OP_LIST_ACTION,                 "\033L" },
  { OP_LIST_REPLY,                  "L" },
  { OP_MAIL,                        "m" },
  { OP_MAIL_KEY,                    "\033k" },
  { OP_MAIN_BREAK_THREAD,           "#" },
  { OP_MAIN_BROWSE_MAILBOXES,       "y" },
  { OP_MAIN_CHANGE_FOLDER,          "c" },
  { OP_MAIN_CHANGE_FOLDER_READONLY, "\033c" },
  { OP_MAIN_CLEAR_FLAG,             "W" },
  { OP_MAIN_COLLAPSE_ALL,           "\033V" },
  { OP_MAIN_COLLAPSE_THREAD,        "\033v" },
  { OP_MAIN_DELETE_PATTERN,         "D" },
#ifdef USE_POP
  { OP_MAIN_FETCH_MAIL,             "G" },
#endif
  { OP_MAIN_LIMIT,                  "l" },
  { OP_MAIN_LINK_THREADS,           "&" },
  { OP_MAIN_NEXT_NEW_THEN_UNREAD,   "\t" },
  { OP_MAIN_NEXT_SUBTHREAD,         "\033n" },
  { OP_MAIN_NEXT_THREAD,            "\016" },
  { OP_MAIN_NEXT_UNDELETED,         "j" },
  { OP_MAIN_NEXT_UNDELETED,         "<down>" },
  { OP_MAIN_PARENT_MESSAGE,         "P" },
  { OP_MAIN_PREV_NEW_THEN_UNREAD,   "\033\t" },
  { OP_MAIN_PREV_SUBTHREAD,         "\033p" },
  { OP_MAIN_PREV_THREAD,            "\020" },
  { OP_MAIN_PREV_UNDELETED,         "k" },
  { OP_MAIN_PREV_UNDELETED,         "<up>" },
  { OP_MAIN_READ_SUBTHREAD,         "\033r" },
  { OP_MAIN_READ_THREAD,            "\022" },
  { OP_MAIN_SET_FLAG,               "w" },
  { OP_MAIN_SHOW_LIMIT,             "\033l" },
  { OP_MAIN_SYNC_FOLDER,            "$" },
  { OP_MAIN_TAG_PATTERN,            "T" },
  { OP_MAIN_UNDELETE_PATTERN,       "U"},
  { OP_MAIN_UNTAG_PATTERN,          "\024" },
  { OP_MARK_MSG,                    "~" },
  { OP_NEXT_ENTRY,                  "J" },
  { OP_PIPE,                        "|" },
  { OP_PREV_ENTRY,                  "K" },
  { OP_PRINT,                       "p" },
  { OP_QUERY,                       "Q" },
  { OP_QUIT,                        "q" },
  { OP_RECALL_MESSAGE,              "R" },
  { OP_REPLY,                       "r" },
  { OP_RESEND,                      "\033e" },
  { OP_SAVE,                        "s" },
  { OP_SORT,                        "o" },
  { OP_SORT_REVERSE,                "O" },
  { OP_TAG_THREAD,                  "\033t" },
  { OP_TOGGLE_NEW,                  "N" },
  { OP_TOGGLE_WRITE,                "%" },
  { OP_UNDELETE,                    "u" },
  { OP_UNDELETE_SUBTHREAD,          "\033u" },
  { OP_UNDELETE_THREAD,             "\025" },
  { OP_VERSION,                     "V" },
  { OP_VIEW_ATTACHMENTS,            "v" },
  { 0,                              NULL }
};

const struct menu_func_op_t OpPager[] = { /* map: pager */
  { "background-compose-menu",   OP_BACKGROUND_COMPOSE_MENU },
  { "bottom",                    OP_PAGER_BOTTOM },
  { "bounce-message",            OP_BOUNCE_MESSAGE },
  { "break-thread",              OP_MAIN_BREAK_THREAD },
  { "browse-mailboxes",          OP_MAIN_BROWSE_MAILBOXES },
  { "browse-mailboxes-readonly", OP_MAIN_BROWSE_MAILBOXES_READONLY },
  { "buffy-list",                OP_BUFFY_LIST },
  { "change-folder",             OP_MAIN_CHANGE_FOLDER },
  { "change-folder-readonly",    OP_MAIN_CHANGE_FOLDER_READONLY },
  { "check-stats",               OP_CHECK_STATS },
  { "check-traditional-pgp",     OP_CHECK_TRADITIONAL },
  { "clear-flag",                OP_MAIN_CLEAR_FLAG },
  { "compose-to-sender",         OP_COMPOSE_TO_SENDER },
  { "copy-message",              OP_COPY_MESSAGE },
  { "create-alias",              OP_CREATE_ALIAS },
  { "decode-copy",               OP_DECODE_COPY },
  { "decode-save",               OP_DECODE_SAVE },
  { "decrypt-copy",              OP_DECRYPT_COPY },
  { "decrypt-save",              OP_DECRYPT_SAVE },
  { "delete-message",            OP_DELETE },
  { "delete-subthread",          OP_DELETE_SUBTHREAD },
  { "delete-thread",             OP_DELETE_THREAD },
  { "display-address",           OP_DISPLAY_ADDRESS },
  { "display-toggle-weed",       OP_DISPLAY_HEADERS },
  { "edit",                      OP_EDIT_MESSAGE },
  { "edit-label",                OP_EDIT_LABEL },
  { "edit-type",                 OP_EDIT_TYPE },
  { "enter-command",             OP_ENTER_COMMAND },
  { "error-history",             OP_ERROR_HISTORY },
  { "exit",                      OP_EXIT },
  { "extract-keys",              OP_EXTRACT_KEYS },
  { "flag-message",              OP_FLAG_MESSAGE },
  { "forget-passphrase",         OP_FORGET_PASSPHRASE },
  { "forward-message",           OP_FORWARD_MESSAGE },
  { "group-chat-reply",          OP_GROUP_CHAT_REPLY },
  { "group-reply",               OP_GROUP_REPLY },
  { "half-down",                 OP_HALF_DOWN },
  { "half-up",                   OP_HALF_UP },
  { "help",                      OP_HELP },
#ifdef USE_IMAP
  { "imap-fetch-mail",           OP_MAIN_IMAP_FETCH },
  { "imap-logout-all",           OP_MAIN_IMAP_LOGOUT_ALL },
#endif
  { "jump",                      OP_JUMP },
  { "link-threads",              OP_MAIN_LINK_THREADS },
  { "list-action",               OP_LIST_ACTION },
  { "list-reply",                OP_LIST_REPLY },
  { "mail",                      OP_MAIL },
  { "mail-key",                  OP_MAIL_KEY },
  { "mark-as-new",               OP_TOGGLE_NEW },
  { "next-entry",                OP_NEXT_ENTRY },
  { "next-line",                 OP_NEXT_LINE },
  { "next-new",                  OP_MAIN_NEXT_NEW },
  { "next-new-then-unread",      OP_MAIN_NEXT_NEW_THEN_UNREAD },
  { "next-page",                 OP_NEXT_PAGE },
  { "next-subthread",            OP_MAIN_NEXT_SUBTHREAD },
  { "next-thread",               OP_MAIN_NEXT_THREAD },
  { "next-undeleted",            OP_MAIN_NEXT_UNDELETED },
  { "next-unread",               OP_MAIN_NEXT_UNREAD },
  { "next-unread-mailbox",       OP_MAIN_NEXT_UNREAD_MAILBOX },
  { "parent-message",            OP_MAIN_PARENT_MESSAGE },
  { "pipe-message",              OP_PIPE },
  { "previous-entry",            OP_PREV_ENTRY },
  { "previous-line",             OP_PREV_LINE },
  { "previous-new",              OP_MAIN_PREV_NEW },
  { "previous-new-then-unread",  OP_MAIN_PREV_NEW_THEN_UNREAD },
  { "previous-page",             OP_PREV_PAGE },
  { "previous-subthread",        OP_MAIN_PREV_SUBTHREAD },
  { "previous-thread",           OP_MAIN_PREV_THREAD },
  { "previous-undeleted",        OP_MAIN_PREV_UNDELETED },
  { "previous-unread",           OP_MAIN_PREV_UNREAD },
  { "print-message",             OP_PRINT },
  { "purge-message",             OP_PURGE_MESSAGE },
  { "quit",                      OP_QUIT },
  { "read-subthread",            OP_MAIN_READ_SUBTHREAD },
  { "read-thread",               OP_MAIN_READ_THREAD },
  { "recall-message",            OP_RECALL_MESSAGE },
  { "redraw-screen",             OP_REDRAW },
  { "reply",                     OP_REPLY },
  { "resend-message",            OP_RESEND },
  { "root-message",              OP_MAIN_ROOT_MESSAGE },
  { "save-message",              OP_SAVE },
  { "search",                    OP_SEARCH },
  { "search-next",               OP_SEARCH_NEXT },
  { "search-opposite",           OP_SEARCH_OPPOSITE },
  { "search-reverse",            OP_SEARCH_REVERSE },
  { "search-toggle",             OP_SEARCH_TOGGLE },
  { "set-flag",                  OP_MAIN_SET_FLAG },
  { "shell-escape",              OP_SHELL_ESCAPE },
  { "show-version",              OP_VERSION },
#ifdef USE_SIDEBAR
  { "sidebar-first",             OP_SIDEBAR_FIRST },
  { "sidebar-last",              OP_SIDEBAR_LAST },
  { "sidebar-next",              OP_SIDEBAR_NEXT },
  { "sidebar-next-new",          OP_SIDEBAR_NEXT_NEW },
  { "sidebar-open",              OP_SIDEBAR_OPEN },
  { "sidebar-page-down",         OP_SIDEBAR_PAGE_DOWN },
  { "sidebar-page-up",           OP_SIDEBAR_PAGE_UP },
  { "sidebar-prev",              OP_SIDEBAR_PREV },
  { "sidebar-prev-new",          OP_SIDEBAR_PREV_NEW },
  { "sidebar-toggle-visible",    OP_SIDEBAR_TOGGLE_VISIBLE },
#endif
  { "skip-headers",              OP_PAGER_SKIP_HEADERS },
  { "skip-quoted",               OP_PAGER_SKIP_QUOTED },
  { "sort-mailbox",              OP_SORT },
  { "sort-reverse",              OP_SORT_REVERSE },
  { "sync-mailbox",              OP_MAIN_SYNC_FOLDER },
  { "tag-message",               OP_TAG },
  { "toggle-quoted",             OP_PAGER_HIDE_QUOTED },
  { "toggle-write",              OP_TOGGLE_WRITE },
  { "top",                       OP_PAGER_TOP },
  { "undelete-message",          OP_UNDELETE },
  { "undelete-subthread",        OP_UNDELETE_SUBTHREAD },
  { "undelete-thread",           OP_UNDELETE_THREAD },
  { "view-attachments",          OP_VIEW_ATTACHMENTS },
  { "what-key",                  OP_WHAT_KEY },
  { NULL,                        0 }
};

const struct menu_op_seq_t PagerDefaultBindings[] = { /* map: pager */
  { OP_BACKGROUND_COMPOSE_MENU,     "B" },
  { OP_BOUNCE_MESSAGE,              "b" },
  { OP_BUFFY_LIST,                  "." },
  { OP_CHECK_TRADITIONAL,           "\033P"   },
  { OP_COPY_MESSAGE,                "C" },
  { OP_CREATE_ALIAS,                "a" },
  { OP_DECODE_COPY,                 "\033C" },
  { OP_DECODE_SAVE,                 "\033s" },
  { OP_DELETE,                      "d" },
  { OP_DELETE_SUBTHREAD,            "\033d" },
  { OP_DELETE_THREAD,               "\004" },
  { OP_DISPLAY_ADDRESS,             "@" },
  { OP_DISPLAY_HEADERS,             "h" },
  { OP_EDIT_LABEL,                  "Y" },
  { OP_EDIT_MESSAGE,                "e" },
  { OP_EDIT_TYPE,                   "\005" },
  { OP_ENTER_COMMAND,               ":" },
  { OP_EXIT,                        "q" },
  { OP_EXIT,                        "i" },
  { OP_EXIT,                        "x" },
  { OP_EXTRACT_KEYS,                "\013" },
  { OP_FLAG_MESSAGE,                "F" },
  { OP_FORGET_PASSPHRASE,           "\006" },
  { OP_FORWARD_MESSAGE,             "f" },
  { OP_GROUP_REPLY,                 "g" },
  { OP_HELP,                        "?" },
  { OP_JUMP,                        "1" },
  { OP_JUMP,                        "2" },
  { OP_JUMP,                        "3" },
  { OP_JUMP,                        "4" },
  { OP_JUMP,                        "5" },
  { OP_JUMP,                        "6" },
  { OP_JUMP,                        "7" },
  { OP_JUMP,                        "8" },
  { OP_JUMP,                        "9" },
  { OP_LIST_ACTION,                 "\033L" },
  { OP_LIST_REPLY,                  "L" },
  { OP_MAIL,                        "m" },
  { OP_MAIL_KEY,                    "\033k" },
  { OP_MAIN_BREAK_THREAD,           "#" },
  { OP_MAIN_BROWSE_MAILBOXES,       "y" },
  { OP_MAIN_CHANGE_FOLDER,          "c" },
  { OP_MAIN_CHANGE_FOLDER_READONLY, "\033c" },
  { OP_MAIN_CLEAR_FLAG,             "W" },
  { OP_MAIN_LINK_THREADS,           "&" },
  { OP_MAIN_NEXT_NEW_THEN_UNREAD,   "\t" },
  { OP_MAIN_NEXT_SUBTHREAD,         "\033n" },
  { OP_MAIN_NEXT_THREAD,            "\016" },
  { OP_MAIN_NEXT_UNDELETED,         "j" },
  { OP_MAIN_NEXT_UNDELETED,         "<down>" },
  { OP_MAIN_NEXT_UNDELETED,         "<right>" },
  { OP_MAIN_PARENT_MESSAGE,         "P" },
  { OP_MAIN_PREV_SUBTHREAD,         "\033p" },
  { OP_MAIN_PREV_THREAD,            "\020" },
  { OP_MAIN_PREV_UNDELETED,         "k" },
  { OP_MAIN_PREV_UNDELETED,         "<left>" },
  { OP_MAIN_PREV_UNDELETED,         "<up>" },
  { OP_MAIN_READ_SUBTHREAD,         "\033r" },
  { OP_MAIN_READ_THREAD,            "\022" },
  { OP_MAIN_SET_FLAG,               "w" },
  { OP_MAIN_SYNC_FOLDER,            "$" },
  { OP_NEXT_ENTRY,                  "J" },
  { OP_NEXT_LINE,                   "\n" },
  { OP_NEXT_LINE,                   "\r" },
  { OP_NEXT_LINE,                   "<keypadenter>" },
  { OP_NEXT_PAGE,                   " " },
  { OP_NEXT_PAGE,                   "<pagedown>" },
  { OP_PAGER_BOTTOM,                "<end>" },
  { OP_PAGER_HIDE_QUOTED,           "T" },
  { OP_PAGER_SKIP_HEADERS,          "H" },
  { OP_PAGER_SKIP_QUOTED,           "S" },
  { OP_PAGER_TOP,                   "^" },
  { OP_PAGER_TOP,                   "<home>" },
  { OP_PIPE,                        "|" },
  { OP_PREV_ENTRY,                  "K" },
  { OP_PREV_LINE,                   "<backspace>" },
  { OP_PREV_PAGE,                   "-" },
  { OP_PREV_PAGE,                   "<pageup>" },
  { OP_PRINT,                       "p" },
  { OP_QUIT,                        "Q" },
  { OP_RECALL_MESSAGE,              "R" },
  { OP_REDRAW,                      "\014" },
  { OP_REPLY,                       "r" },
  { OP_RESEND,                      "\033e" },
  { OP_SAVE,                        "s" },
  { OP_SEARCH,                      "/" },
  { OP_SEARCH_NEXT,                 "n" },
  { OP_SEARCH_REVERSE,              "\033/" },
  { OP_SEARCH_TOGGLE,               "\\" },
  { OP_SHELL_ESCAPE,                "!" },
  { OP_SORT,                        "o" },
  { OP_SORT_REVERSE,                "O" },
  { OP_TAG,                         "t" },
  { OP_TOGGLE_NEW,                  "N" },
  { OP_TOGGLE_WRITE,                "%" },
  { OP_UNDELETE,                    "u" },
  { OP_UNDELETE_SUBTHREAD,          "\033u" },
  { OP_UNDELETE_THREAD,             "\025" },
  { OP_VERSION,                     "V" },
  { OP_VIEW_ATTACHMENTS,            "v" },
  { 0,                              NULL }
};

const struct menu_func_op_t OpAttach[] = { /* map: attachment */
  { "bounce-message",        OP_BOUNCE_MESSAGE },
  { "check-traditional-pgp", OP_CHECK_TRADITIONAL },
  { "collapse-parts",        OP_ATTACH_COLLAPSE },
  { "compose-to-sender",     OP_COMPOSE_TO_SENDER },
  { "delete-entry",          OP_DELETE },
  { "display-toggle-weed",   OP_DISPLAY_HEADERS },
  { "edit-type",             OP_EDIT_TYPE },
  { "extract-keys",          OP_EXTRACT_KEYS },
  { "forget-passphrase",     OP_FORGET_PASSPHRASE },
  { "forward-message",       OP_FORWARD_MESSAGE },
  { "group-chat-reply",      OP_GROUP_CHAT_REPLY },
  { "group-reply",           OP_GROUP_REPLY },
  { "list-reply",            OP_LIST_REPLY },
  { "pipe-entry",            OP_PIPE },
  { "print-entry",           OP_PRINT },
  { "reply",                 OP_REPLY },
  { "resend-message",        OP_RESEND },
  { "save-entry",            OP_SAVE },
  { "undelete-entry",        OP_UNDELETE },
  { "view-attach",           OP_VIEW_ATTACH },
  { "view-mailcap",          OP_ATTACH_VIEW_MAILCAP },
  { "view-pager",            OP_ATTACH_VIEW_PAGER },
  { "view-text",             OP_ATTACH_VIEW_TEXT },
  { NULL,                    0 }
};

const struct menu_op_seq_t AttachDefaultBindings[] = { /* map: attachment */
  { OP_ATTACH_COLLAPSE,     "v" },
  { OP_ATTACH_VIEW_MAILCAP, "m" },
  { OP_ATTACH_VIEW_TEXT,    "T" },
  { OP_BOUNCE_MESSAGE,      "b" },
  { OP_CHECK_TRADITIONAL,   "\033P"   },
  { OP_DELETE,              "d" },
  { OP_DISPLAY_HEADERS,     "h" },
  { OP_EDIT_TYPE,           "\005" },
  { OP_EXTRACT_KEYS,        "\013" },
  { OP_FORGET_PASSPHRASE,   "\006" },
  { OP_FORWARD_MESSAGE,     "f" },
  { OP_GROUP_REPLY,         "g" },
  { OP_LIST_REPLY,          "L" },
  { OP_PIPE,                "|" },
  { OP_PRINT,               "p" },
  { OP_REPLY,               "r" },
  { OP_RESEND,              "\033e" },
  { OP_SAVE,                "s" },
  { OP_UNDELETE,            "u" },
  { OP_VIEW_ATTACH,         "\n" },
  { OP_VIEW_ATTACH,         "\r" },
  { OP_VIEW_ATTACH,         "<keypadenter>" },
  { 0,                      NULL }
};

const struct menu_func_op_t OpCompose[] = { /* map: compose */
  { "attach-file",         OP_COMPOSE_ATTACH_FILE },
  { "attach-key",          OP_COMPOSE_ATTACH_KEY },
  { "attach-message",      OP_COMPOSE_ATTACH_MESSAGE },
#ifdef USE_AUTOCRYPT
  { "autocrypt-menu",      OP_COMPOSE_AUTOCRYPT_MENU },
#endif
  { "copy-file",           OP_SAVE },
  { "detach-file",         OP_DELETE },
  { "display-toggle-weed", OP_DISPLAY_HEADERS },
  { "edit-bcc",            OP_COMPOSE_EDIT_BCC },
  { "edit-cc",             OP_COMPOSE_EDIT_CC },
  { "edit-description",    OP_COMPOSE_EDIT_DESCRIPTION },
  { "edit-encoding",       OP_COMPOSE_EDIT_ENCODING },
  { "edit-fcc",            OP_COMPOSE_EDIT_FCC },
  { "edit-file",           OP_COMPOSE_EDIT_FILE },
  { "edit-from",           OP_COMPOSE_EDIT_FROM },
  { "edit-headers",        OP_COMPOSE_EDIT_HEADERS },
  { "edit-message",        OP_COMPOSE_EDIT_MESSAGE },
  { "edit-mime",           OP_COMPOSE_EDIT_MIME },
  { "edit-reply-to",       OP_COMPOSE_EDIT_REPLY_TO },
  { "edit-subject",        OP_COMPOSE_EDIT_SUBJECT },
  { "edit-to",             OP_COMPOSE_EDIT_TO },
  { "edit-type",           OP_EDIT_TYPE },
  { "filter-entry",        OP_FILTER },
  { "forget-passphrase",   OP_FORGET_PASSPHRASE },
  { "get-attachment",      OP_COMPOSE_GET_ATTACHMENT },
  { "ispell",              OP_COMPOSE_ISPELL },
#ifdef MIXMASTER
  { "mix",                 OP_COMPOSE_MIX },
#endif
  { "move-down",           OP_COMPOSE_MOVE_DOWN },
  { "move-up",             OP_COMPOSE_MOVE_UP },
  { "new-mime",            OP_COMPOSE_NEW_MIME },
  { "pgp-menu",            OP_COMPOSE_PGP_MENU },
  { "pipe-entry",          OP_PIPE },
  { "postpone-message",    OP_COMPOSE_POSTPONE_MESSAGE },
  { "print-entry",         OP_PRINT },
  { "rename-attachment",   OP_COMPOSE_RENAME_ATTACHMENT },
  { "rename-file",         OP_COMPOSE_RENAME_FILE },
  { "send-message",        OP_COMPOSE_SEND_MESSAGE },
  { "smime-menu",          OP_COMPOSE_SMIME_MENU },
  { "toggle-disposition",  OP_COMPOSE_TOGGLE_DISPOSITION },
  { "toggle-recode",       OP_COMPOSE_TOGGLE_RECODE },
  { "toggle-unlink",       OP_COMPOSE_TOGGLE_UNLINK },
  { "update-encoding",     OP_COMPOSE_UPDATE_ENCODING },
  { "view-alt",            OP_COMPOSE_VIEW_ALT },
  { "view-alt-mailcap",    OP_COMPOSE_VIEW_ALT_MAILCAP },
  { "view-alt-pager",      OP_COMPOSE_VIEW_ALT_PAGER },
  { "view-alt-text",       OP_COMPOSE_VIEW_ALT_TEXT },
  { "view-attach",         OP_VIEW_ATTACH },
  { "view-mailcap",        OP_ATTACH_VIEW_MAILCAP },
  { "view-pager",          OP_ATTACH_VIEW_PAGER },
  { "view-text",           OP_ATTACH_VIEW_TEXT },
  { "write-fcc",           OP_COMPOSE_WRITE_MESSAGE },
  { NULL,                  0 }
};

const struct menu_op_seq_t ComposeDefaultBindings[] = { /* map: compose */
  { OP_COMPOSE_ATTACH_FILE,        "a" },
  { OP_COMPOSE_ATTACH_KEY,         "\033k" },
  { OP_COMPOSE_ATTACH_MESSAGE,     "A" },
#ifdef USE_AUTOCRYPT
  { OP_COMPOSE_AUTOCRYPT_MENU,     "o" },
#endif
  { OP_COMPOSE_EDIT_BCC,           "b" },
  { OP_COMPOSE_EDIT_CC,            "c" },
  { OP_COMPOSE_EDIT_DESCRIPTION,   "d" },
  { OP_COMPOSE_EDIT_ENCODING,      "\005" },
  { OP_COMPOSE_EDIT_FCC,           "f" },
  { OP_COMPOSE_EDIT_FILE,          "\030e" },
  { OP_COMPOSE_EDIT_FROM,          "\033f" },
  { OP_COMPOSE_EDIT_HEADERS,       "E" },
  { OP_COMPOSE_EDIT_MESSAGE,       "e" },
  { OP_COMPOSE_EDIT_MIME,          "m" },
  { OP_COMPOSE_EDIT_REPLY_TO,      "r" },
  { OP_COMPOSE_EDIT_SUBJECT,       "s" },
  { OP_COMPOSE_EDIT_TO,            "t" },
  { OP_COMPOSE_GET_ATTACHMENT,     "G" },
  { OP_COMPOSE_ISPELL,             "i" },
#ifdef MIXMASTER
  { OP_COMPOSE_MIX,                "M" },
#endif
  { OP_COMPOSE_NEW_MIME,           "n" },
  { OP_COMPOSE_PGP_MENU,           "p"  },
  { OP_COMPOSE_POSTPONE_MESSAGE,   "P" },
  { OP_COMPOSE_RENAME_ATTACHMENT,  "\017" },
  { OP_COMPOSE_RENAME_FILE,        "R" },
  { OP_COMPOSE_SEND_MESSAGE,       "y" },
  { OP_COMPOSE_SMIME_MENU,         "S"  },
  { OP_COMPOSE_TOGGLE_DISPOSITION, "\004" },
  { OP_COMPOSE_TOGGLE_UNLINK,      "u" },
  { OP_COMPOSE_UPDATE_ENCODING,    "U" },
  { OP_COMPOSE_VIEW_ALT,           "v" },
  { OP_COMPOSE_VIEW_ALT_MAILCAP,   "V" },
  { OP_COMPOSE_VIEW_ALT_TEXT,      "\033v" },
  { OP_COMPOSE_WRITE_MESSAGE,      "w" },
  { OP_DELETE,                     "D" },
  { OP_DISPLAY_HEADERS,            "h" },
  { OP_EDIT_TYPE,                  "\024" },
  { OP_FILTER,                     "F" },
  { OP_FORGET_PASSPHRASE,          "\006"  },
  { OP_PIPE,                       "|" },
  { OP_PRINT,                      "l" },
  { OP_SAVE,                       "C" },
  { OP_TAG,                        "T" },
  { OP_VIEW_ATTACH,                "\n" },
  { OP_VIEW_ATTACH,                "\r" },
  { OP_VIEW_ATTACH,                "<keypadenter>" },
  { 0,                             NULL }
};

const struct menu_func_op_t OpPost[] = { /* map: postpone */
  { "delete-entry",     OP_DELETE },
  { "undelete-entry",   OP_UNDELETE },
  { NULL,               0 }
};

const struct menu_op_seq_t PostDefaultBindings[] = { /* map: postpone */
  { OP_DELETE,   "d" },
  { OP_UNDELETE, "u" },
  { 0,           NULL }
};

const struct menu_func_op_t OpAlias[] = { /* map: alias */
  { "delete-entry",     OP_DELETE },
  { "undelete-entry",   OP_UNDELETE },
  { NULL,               0 }
};

const struct menu_op_seq_t AliasDefaultBindings[] = { /* map: alias */
  { OP_DELETE,   "d" },
  { OP_TAG,      "<space>" },
  { OP_UNDELETE, "u" },
  { 0,           NULL }
};


/* The file browser */
const struct menu_func_op_t OpBrowser[] = { /* map: browser */
  { "buffy-list",        OP_BUFFY_LIST },
  { "change-dir",        OP_CHANGE_DIRECTORY },
  { "check-new",         OP_CHECK_NEW },
  { "descend-directory", OP_DESCEND_DIRECTORY },
  { "display-filename",  OP_BROWSER_TELL },
  { "enter-mask",        OP_ENTER_MASK },
  { "select-new",        OP_BROWSER_NEW_FILE },
  { "sort",              OP_SORT },
  { "sort-reverse",      OP_SORT_REVERSE },
  { "toggle-mailboxes",  OP_TOGGLE_MAILBOXES },
  { "view-file",         OP_BROWSER_VIEW_FILE },
#ifdef USE_IMAP
  { "create-mailbox",    OP_CREATE_MAILBOX },
  { "delete-mailbox",    OP_DELETE_MAILBOX },
  { "rename-mailbox",    OP_RENAME_MAILBOX },
  { "subscribe",         OP_BROWSER_SUBSCRIBE },
  { "toggle-subscribed", OP_BROWSER_TOGGLE_LSUB },
  { "unsubscribe",       OP_BROWSER_UNSUBSCRIBE },
#endif
  { NULL,                0 }
};

const struct menu_op_seq_t BrowserDefaultBindings[] = { /* map: browser */
  { OP_BROWSER_NEW_FILE,    "N" },
  { OP_BROWSER_TELL,        "@" },
  { OP_BROWSER_VIEW_FILE,   " " },
  { OP_BUFFY_LIST,          "." },
  { OP_CHANGE_DIRECTORY,    "c" },
  { OP_ENTER_MASK,          "m" },
  { OP_SORT,                "o" },
  { OP_SORT_REVERSE,        "O" },
  { OP_TOGGLE_MAILBOXES,    "\t" },
#ifdef USE_IMAP
  { OP_BROWSER_SUBSCRIBE,   "s" },
  { OP_BROWSER_TOGGLE_LSUB, "T" },
  { OP_BROWSER_UNSUBSCRIBE, "u" },
  { OP_CREATE_MAILBOX,      "C" },
  { OP_DELETE_MAILBOX,      "d" },
  { OP_RENAME_MAILBOX,      "r" },
#endif
  { 0,                      NULL }
};

/* External Query Menu */
const struct menu_func_op_t OpQuery[] = { /* map: query */
  { "create-alias",     OP_CREATE_ALIAS },
  { "mail",             OP_MAIL },
  { "query",            OP_QUERY },
  { "query-append",     OP_QUERY_APPEND },
  { NULL,               0 }
};

const struct menu_op_seq_t QueryDefaultBindings[] = { /* map: query */
  { OP_CREATE_ALIAS,    "a" },
  { OP_MAIL,            "m" },
  { OP_QUERY,           "Q" },
  { OP_QUERY_APPEND,    "A" },
  { 0,                  NULL }
};

const struct menu_func_op_t OpEditor[] = { /* map: editor */
  { "backspace",        OP_EDITOR_BACKSPACE },
  { "backward-char",    OP_EDITOR_BACKWARD_CHAR },
  { "backward-word",    OP_EDITOR_BACKWARD_WORD },
  { "bol",              OP_EDITOR_BOL },
  { "buffy-cycle",      OP_EDITOR_BUFFY_CYCLE },
  { "capitalize-word",  OP_EDITOR_CAPITALIZE_WORD },
  { "complete",         OP_EDITOR_COMPLETE },
  { "complete-query",   OP_EDITOR_COMPLETE_QUERY },
  { "delete-char",      OP_EDITOR_DELETE_CHAR },
  { "downcase-word",    OP_EDITOR_DOWNCASE_WORD },
  { "eol",              OP_EDITOR_EOL },
  { "forward-char",     OP_EDITOR_FORWARD_CHAR },
  { "forward-word",     OP_EDITOR_FORWARD_WORD },
  { "history-down",     OP_EDITOR_HISTORY_DOWN },
  { "history-search",   OP_EDITOR_HISTORY_SEARCH },
  { "history-up",       OP_EDITOR_HISTORY_UP },
  { "kill-eol",         OP_EDITOR_KILL_EOL },
  { "kill-eow",         OP_EDITOR_KILL_EOW },
  { "kill-line",        OP_EDITOR_KILL_LINE },
  { "kill-word",        OP_EDITOR_KILL_WORD },
  { "quote-char",       OP_EDITOR_QUOTE_CHAR },
  { "transpose-chars",  OP_EDITOR_TRANSPOSE_CHARS },
  { "upcase-word",      OP_EDITOR_UPCASE_WORD },
  { NULL,               0 }
};

const struct menu_op_seq_t EditorDefaultBindings[] = { /* map: editor */
  { OP_EDITOR_BACKSPACE,       "\010" },
  { OP_EDITOR_BACKSPACE,       "<backspace>" },
  { OP_EDITOR_BACKSPACE,       "<delete>" },
  { OP_EDITOR_BACKSPACE,       "\177" },
  { OP_EDITOR_BACKWARD_CHAR,   "\002" },
  { OP_EDITOR_BACKWARD_CHAR,   "<left>" },
  { OP_EDITOR_BACKWARD_WORD,   "\033b"},
  { OP_EDITOR_BOL,             "\001" },
  { OP_EDITOR_BOL,             "<home>" },
  { OP_EDITOR_BUFFY_CYCLE,     " "    },
  { OP_EDITOR_CAPITALIZE_WORD, "\033c"},
  { OP_EDITOR_COMPLETE,        "\t"   },
  { OP_EDITOR_COMPLETE_QUERY,  "\024" },
  { OP_EDITOR_DELETE_CHAR,     "\004" },
  { OP_EDITOR_DOWNCASE_WORD,   "\033l"},
  { OP_EDITOR_EOL,             "\005" },
  { OP_EDITOR_EOL,             "<end>" },
  { OP_EDITOR_FORWARD_CHAR,    "\006" },
  { OP_EDITOR_FORWARD_CHAR,    "<right>" },
  { OP_EDITOR_FORWARD_WORD,    "\033f"},
  { OP_EDITOR_HISTORY_DOWN,    "\016" },
  { OP_EDITOR_HISTORY_DOWN,    "<down>" },
  { OP_EDITOR_HISTORY_SEARCH,  "\022" },
  { OP_EDITOR_HISTORY_UP,      "\020" },
  { OP_EDITOR_HISTORY_UP,      "<up>" },
  { OP_EDITOR_KILL_EOL,        "\013" },
  { OP_EDITOR_KILL_EOW,        "\033d"},
  { OP_EDITOR_KILL_LINE,       "\025" },
  { OP_EDITOR_KILL_WORD,       "\027" },
  { OP_EDITOR_QUOTE_CHAR,      "\026" },
  { OP_EDITOR_UPCASE_WORD,     "\033u"},
  { 0,                         NULL   }
};


const struct menu_func_op_t OpPgp[] = { /* map: pgp */
  { "verify-key",       OP_VERIFY_KEY },
  { "view-name",        OP_VIEW_ID },
  { NULL,               0 }
};

const struct menu_op_seq_t PgpDefaultBindings[] = { /* map: pgp */
  { OP_VERIFY_KEY, "c" },
  { OP_VIEW_ID,    "%" },
  { 0,             NULL }
};


const struct menu_func_op_t OpList[] = { /* map: list */
  { "list-archive",     OP_LIST_ARCHIVE },
  { "list-help",        OP_LIST_HELP },
  { "list-owner",       OP_LIST_OWNER },
  { "list-post",        OP_LIST_POST },
  { "list-subscribe",   OP_LIST_SUBSCRIBE },
  { "list-unsubscribe", OP_LIST_UNSUBSCRIBE },
  { NULL,               0 }
};

const struct menu_op_seq_t ListDefaultBindings[] = { /* map: list */
  { OP_LIST_ARCHIVE,     "a" },
  { OP_LIST_HELP,        "h" },
  { OP_LIST_OWNER,       "o" },
  { OP_LIST_POST,        "p" },
  { OP_LIST_SUBSCRIBE,   "s" },
  { OP_LIST_UNSUBSCRIBE, "u" },
  { 0,                   NULL }
};


/* When using the GPGME based backend we have some useful functions
   for the SMIME menu.  */
const struct menu_func_op_t OpSmime[] = { /* map: smime */
#ifdef CRYPT_BACKEND_GPGME
  { "verify-key",    OP_VERIFY_KEY },
  { "view-name",     OP_VIEW_ID },
#endif
  { NULL,       0 }
};

const struct menu_op_seq_t SmimeDefaultBindings[] = { /* map: smime */
#ifdef CRYPT_BACKEND_GPGME
  { OP_VERIFY_KEY, "c" },
  { OP_VIEW_ID,    "%" },
#endif
  { 0,             NULL }
};



#ifdef MIXMASTER
const struct menu_func_op_t OpMix[] = { /* map: mixmaster */
  { "accept",           OP_MIX_USE },
  { "append",           OP_MIX_APPEND },
  { "chain-next",       OP_MIX_CHAIN_NEXT },
  { "chain-prev",       OP_MIX_CHAIN_PREV },
  { "delete",           OP_MIX_DELETE },
  { "insert",           OP_MIX_INSERT },
  { NULL,               0 }
};

const struct menu_op_seq_t MixDefaultBindings[] = { /* map: mixmaster */
  { OP_GENERIC_SELECT_ENTRY, "<space>" },
  { OP_MIX_APPEND,           "a"       },
  { OP_MIX_CHAIN_NEXT,       "<right>" },
  { OP_MIX_CHAIN_NEXT,       "l" },
  { OP_MIX_CHAIN_PREV,       "<left>" },
  { OP_MIX_CHAIN_PREV,       "h" },
  { OP_MIX_DELETE,           "d"          },
  { OP_MIX_INSERT,           "i"       },
  { OP_MIX_USE,              "\n" },
  { OP_MIX_USE,              "\r" },
  { OP_MIX_USE,              "<keypadenter>" },
  { 0,                       NULL }
};
#endif /* MIXMASTER */

#ifdef USE_AUTOCRYPT
const struct menu_func_op_t OpAutocryptAcct[] = { /* map: autocrypt account */
  { "create-account",        OP_AUTOCRYPT_CREATE_ACCT },
  { "delete-account",        OP_AUTOCRYPT_DELETE_ACCT },
  { "toggle-active",         OP_AUTOCRYPT_TOGGLE_ACTIVE },
  { "toggle-prefer-encrypt", OP_AUTOCRYPT_TOGGLE_PREFER },
  { NULL,                    0 }
};

const struct menu_op_seq_t AutocryptAcctDefaultBindings[] = { /* map: autocrypt account */
  { OP_AUTOCRYPT_CREATE_ACCT,    "c" },
  { OP_AUTOCRYPT_DELETE_ACCT,    "D" },
  { OP_AUTOCRYPT_TOGGLE_ACTIVE,  "a" },
  { OP_AUTOCRYPT_TOGGLE_PREFER,  "p" },
  { 0,                            NULL }
};
#endif
