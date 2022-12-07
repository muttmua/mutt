/*
 * Copyright (C) 1996-2002,2007,2010,2012-2013,2016 Michael R. Elkins <me@mutt.org>
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

#ifdef _MAKEDOC
# include "config.h"
# include "doc/makedoc-defs.h"
#else
# include "sort.h"
#endif

#include "buffy.h"

#ifndef _MAKEDOC
/* If you add a data type, be sure to update doc/makedoc.pl */
#define DT_MASK		0x0f
#define DT_BOOL		1 /* boolean option */
#define DT_NUM		2 /* a number (short) */
#define DT_STR		3 /* a string */
#define DT_PATH		4 /* a pathname */
#define DT_CMD_PATH	5 /* a pathname for a command: no relpath expansion */
#define DT_QUAD		6 /* quad-option (yes/no/ask-yes/ask-no) */
#define DT_SORT		7 /* sorting methods */
#define DT_RX		8 /* regular expressions */
#define DT_MAGIC	9 /* mailbox type */
#define DT_SYN	       10 /* synonym for another variable */
#define DT_ADDR	       11 /* e-mail address */
#define DT_MBCHARTBL   12 /* multibyte char table */
#define DT_LNUM        13 /* a number (long) */

#define DTYPE(x) ((x) & DT_MASK)

/* subtypes */
#define DT_SUBTYPE_MASK       0xff0
#define DT_SORT_ALIAS         0x10
#define DT_SORT_BROWSER       0x20
#define DT_SORT_KEYS          0x40
#define DT_SORT_AUX           0x80
#define DT_SORT_SIDEBAR       0x100
#define DT_L10N_STR           0x200
#define DT_SORT_THREAD_GROUPS 0x400

/* flags to parse_set() */
#define MUTT_SET_INV	(1<<0)	/* default is to invert all vars */
#define MUTT_SET_UNSET	(1<<1)	/* default is to unset all vars */
#define MUTT_SET_RESET	(1<<2)	/* default is to reset all vars to default */

/* forced redraw/resort types */
#define R_NONE		0
#define R_INDEX		(1<<0)  /* redraw the index menu (MENU_MAIN) */
#define R_PAGER		(1<<1)  /* redraw the pager menu */
#define R_PAGER_FLOW    (1<<2)  /* reflow lineInfo and redraw the pager menu */
#define R_RESORT	(1<<3)	/* resort the mailbox */
#define R_RESORT_SUB	(1<<4)	/* resort subthreads */
#define R_RESORT_INIT	(1<<5)  /* resort from scratch */
#define R_TREE		(1<<6)  /* redraw the thread tree */
#define R_REFLOW        (1<<7)  /* reflow window layout and full redraw */
#define R_SIDEBAR       (1<<8)  /* redraw the sidebar */
#define R_MENU          (1<<9)  /* redraw all menus */
#define R_BOTH		(R_INDEX | R_PAGER)
#define R_RESORT_BOTH	(R_RESORT | R_RESORT_SUB)

struct option_t
{
  char *option;
  short type;
  short flags;
  union pointer_long_t data;
  union pointer_long_t init; /* initial value */
};
#endif /* _MAKEDOC */

#ifndef ISPELL
#define ISPELL "ispell"
#endif

/* Sort Maps:
 * Value-to-name lookup uses the first match.  They are sorted by value
 * to make the duplicate values more obvious. */

/*+sort+*/
const struct mapping_t SortMethods[] = {  /* DT_SORT */
  { "date",		SORT_DATE },
  { "date-sent",	SORT_DATE },
  { "from",		SORT_FROM },
  { "label",		SORT_LABEL },
  { "mailbox-order",	SORT_ORDER },
  { "date-received",	SORT_RECEIVED },
  { "score",		SORT_SCORE },
  { "size",		SORT_SIZE },
  { "spam",		SORT_SPAM },
  { "subject",		SORT_SUBJECT },
  { "threads",		SORT_THREADS },
  { "to",		SORT_TO },
  { NULL,               0 }
};

/* same as SortMethods, but with "threads" replaced by "date" */

const struct mapping_t SortAuxMethods[] = {  /* DT_SORT_AUX */
  { "date",		SORT_DATE },
  { "date-sent",	SORT_DATE },
  { "threads",		SORT_DATE },	/* note: sort_aux == threads
					 * isn't possible.
					 */
  { "from",		SORT_FROM },
  { "label",		SORT_LABEL },
  { "mailbox-order",	SORT_ORDER },
  { "date-received",	SORT_RECEIVED },
  { "score",		SORT_SCORE },
  { "size",		SORT_SIZE },
  { "spam",		SORT_SPAM },
  { "subject",		SORT_SUBJECT },
  { "to",		SORT_TO },
  { NULL,               0 }
};

/* Used by $sort_thread_groups.  "aux" delegates to $sort_aux */
const struct mapping_t SortThreadGroupsMethods[] = {  /* DT_SORT_THREAD_GROUPS */
  { "aux",		SORT_AUX },
  { "date",		SORT_DATE },
  { "date-sent",	SORT_DATE },
  { "from",		SORT_FROM },
  { "label",		SORT_LABEL },
  { "mailbox-order",	SORT_ORDER },
  { "date-received",	SORT_RECEIVED },
  { "score",		SORT_SCORE },
  { "size",		SORT_SIZE },
  { "spam",		SORT_SPAM },
  { "subject",		SORT_SUBJECT },
  { "threads",		SORT_THREADS },
  { "to",		SORT_TO },
  { NULL,               0 }
};

const struct mapping_t SortBrowserMethods[] = {  /* DT_SORT_BROWSER */
  { "count",	SORT_COUNT },
  { "date",	SORT_DATE },
  { "unsorted",	SORT_ORDER },
  { "size",	SORT_SIZE },
  { "alpha",	SORT_SUBJECT },
  { "unread",	SORT_UNREAD },
  { NULL,       0 }
};

const struct mapping_t SortAliasMethods[] = {  /* DT_SORT_ALIAS */
  { "address",	SORT_ADDRESS },
  { "alias",	SORT_ALIAS },
  { "unsorted", SORT_ORDER },
  { NULL,       0 }
};

const struct mapping_t SortKeyMethods[] = {  /* DT_SORT_KEYS */
  { "address",	SORT_ADDRESS },
  { "date",	SORT_DATE },
  { "keyid",	SORT_KEYID },
  { "trust",	SORT_TRUST },
  { NULL,       0 }
};

const struct mapping_t SortSidebarMethods[] = {  /* DT_SORT_SIDEBAR */
  { "count",		SORT_COUNT },
  { "flagged",		SORT_FLAGGED },
  { "unsorted",		SORT_ORDER },
  { "mailbox-order",	SORT_ORDER },
  { "path",		SORT_PATH },
  { "alpha",		SORT_SUBJECT },
  { "name",		SORT_SUBJECT },
  { "unread",		SORT_UNREAD },
  { "new",		SORT_UNREAD },  /* kept for compatibility */
  { NULL,		0 }
};
/*-sort-*/


struct option_t MuttVars[] = {
  /*++*/
  { "abort_noattach", DT_QUAD, R_NONE, {.l=OPT_ABORTNOATTACH}, {.l=MUTT_NO} },
  /*
  ** .pp
  ** When the body of the message matches $$abort_noattach_regexp and
  ** there are no attachments, this quadoption controls whether to
  ** abort sending the message.
  */
  { "abort_noattach_regexp",  DT_RX,  R_NONE, {.p=&AbortNoattachRegexp}, {.p="attach"} },
  /*
  ** .pp
  ** Specifies a regular expression to match against the body of the
  ** message, to determine if an attachment was mentioned but
  ** mistakenly forgotten.  If it matches, $$abort_noattach will be
  ** consulted to determine if message sending will be aborted.
  ** .pp
  ** Like other regular expressions in Mutt, the search is case
  ** sensitive if the pattern contains at least one upper case letter,
  ** and case insensitive otherwise.
  */
  { "abort_nosubject",	DT_QUAD, R_NONE, {.l=OPT_SUBJECT}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** If set to \fIyes\fP, when composing messages and no subject is given
  ** at the subject prompt, composition will be aborted.  If set to
  ** \fIno\fP, composing messages with no subject given at the subject
  ** prompt will never be aborted.
  */
  { "abort_unmodified",	DT_QUAD, R_NONE, {.l=OPT_ABORT}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** If set to \fIyes\fP, composition will automatically abort after
  ** editing the message body if no changes are made to the file (this
  ** check only happens after the \fIfirst\fP edit of the file).  When set
  ** to \fIno\fP, composition will never be aborted.
  */
  { "alias_file",	DT_PATH, R_NONE, {.p=&AliasFile}, {.p="~/.muttrc"} },
  /*
  ** .pp
  ** The default file in which to save aliases created by the
  ** \fC$<create-alias>\fP function. Entries added to this file are
  ** encoded in the character set specified by $$config_charset if it
  ** is \fIset\fP or the current character set otherwise.
  ** .pp
  ** \fBNote:\fP Mutt will not automatically source this file; you must
  ** explicitly use the ``$source'' command for it to be executed in case
  ** this option points to a dedicated alias file.
  ** .pp
  ** The default for this option is the currently used muttrc file, or
  ** ``~/.muttrc'' if no user muttrc was found.
  */
  { "alias_format",	DT_STR,  R_NONE, {.p=&AliasFmt}, {.p="%4n %2f %t %-10a   %r"} },
  /*
  ** .pp
  ** Specifies the format of the data displayed for the ``$alias'' menu.  The
  ** following \fCprintf(3)\fP-style sequences are available:
  ** .dl
  ** .dt %a .dd alias name
  ** .dt %f .dd flags - currently, a ``d'' for an alias marked for deletion
  ** .dt %n .dd index number
  ** .dt %r .dd address which alias expands to
  ** .dt %t .dd character which indicates if the alias is tagged for inclusion
  ** .de
  */
  { "allow_8bit",	DT_BOOL, R_NONE, {.l=OPTALLOW8BIT}, {.l=1} },
  /*
  ** .pp
  ** Controls whether 8-bit data is converted to 7-bit using either Quoted-
  ** Printable or Base64 encoding when sending mail.
  */
  { "allow_ansi",      DT_BOOL, R_NONE, {.l=OPTALLOWANSI}, {.l=0} },
  /*
  ** .pp
  ** Controls whether ANSI color codes in messages (and color tags in
  ** rich text messages) are to be interpreted.
  ** Messages containing these codes are rare, but if this option is \fIset\fP,
  ** their text will be colored accordingly. Note that this may override
  ** your color choices, and even present a security problem, since a
  ** message could include a line like
  ** .ts
  ** [-- PGP output follows ...
  ** .te
  ** .pp
  ** and give it the same color as your attachment color (see also
  ** $$crypt_timestamp).
  */
  { "arrow_cursor",	DT_BOOL, R_MENU, {.l=OPTARROWCURSOR}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, an arrow (``->'') will be used to indicate the current entry
  ** in menus instead of highlighting the whole line.  On slow network or modem
  ** links this will make response faster because there is less that has to
  ** be redrawn on the screen when moving to the next or previous entries
  ** in the menu.
  */
  { "ascii_chars",	DT_BOOL, R_BOTH, {.l=OPTASCIICHARS}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will use plain ASCII characters when displaying thread
  ** and attachment trees, instead of the default \fIACS\fP characters.
  */
  { "askbcc",		DT_BOOL, R_NONE, {.l=OPTASKBCC}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will prompt you for blind-carbon-copy (Bcc) recipients
  ** before editing an outgoing message.
  */
  { "askcc",		DT_BOOL, R_NONE, {.l=OPTASKCC}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will prompt you for carbon-copy (Cc) recipients before
  ** editing the body of an outgoing message.
  */
  { "assumed_charset", DT_STR, R_NONE, {.p=&AssumedCharset}, {.p=0} },
  /*
  ** .pp
  ** This variable is a colon-separated list of character encoding
  ** schemes for messages without character encoding indication.
  ** Header field values and message body content without character encoding
  ** indication would be assumed that they are written in one of this list.
  ** By default, all the header fields and message body without any charset
  ** indication are assumed to be in ``us-ascii''.
  ** .pp
  ** For example, Japanese users might prefer this:
  ** .ts
  ** set assumed_charset="iso-2022-jp:euc-jp:shift_jis:utf-8"
  ** .te
  ** .pp
  ** However, only the first content is valid for the message body.
  */
  { "attach_charset",    DT_STR,  R_NONE, {.p=&AttachCharset}, {.p=0} },
  /*
  ** .pp
  ** This variable is a colon-separated list of character encoding
  ** schemes for text file attachments. Mutt uses this setting to guess
  ** which encoding files being attached are encoded in to convert them to
  ** a proper character set given in $$send_charset.
  ** .pp
  ** If \fIunset\fP, the value of $$charset will be used instead.
  ** For example, the following configuration would work for Japanese
  ** text handling:
  ** .ts
  ** set attach_charset="iso-2022-jp:euc-jp:shift_jis:utf-8"
  ** .te
  ** .pp
  ** Note: for Japanese users, ``iso-2022-*'' must be put at the head
  ** of the value as shown above if included.
  */
  { "attach_format",	DT_STR,  R_NONE, {.p=&AttachFormat}, {.p="%u%D%I %t%4n %T%.40d%> [%.7m/%.10M, %.6e%?C?, %C?, %s] "} },
  /*
  ** .pp
  ** This variable describes the format of the ``attachment'' menu.  The
  ** following \fCprintf(3)\fP-style sequences are understood:
  ** .dl
  ** .dt %C  .dd charset
  ** .dt %c  .dd requires charset conversion (``n'' or ``c'')
  ** .dt %D  .dd deleted flag
  ** .dt %d  .dd description (if none, falls back to %F)
  ** .dt %e  .dd MIME content-transfer-encoding
  ** .dt %F  .dd filename in content-disposition header (if none, falls back to %f)
  ** .dt %f  .dd filename
  ** .dt %I  .dd disposition (``I'' for inline, ``A'' for attachment)
  ** .dt %m  .dd major MIME type
  ** .dt %M  .dd MIME subtype
  ** .dt %n  .dd attachment number
  ** .dt %Q  .dd ``Q'', if MIME part qualifies for attachment counting
  ** .dt %s  .dd size (see $formatstrings-size)
  ** .dt %t  .dd tagged flag
  ** .dt %T  .dd graphic tree characters
  ** .dt %u  .dd unlink (=to delete) flag
  ** .dt %X  .dd number of qualifying MIME parts in this part and its children
  **             (please see the ``$attachments'' section for possible speed effects)
  ** .dt %>X .dd right justify the rest of the string and pad with character ``X''
  ** .dt %|X .dd pad to the end of the line with character ``X''
  ** .dt %*X .dd soft-fill with character ``X'' as pad
  ** .de
  ** .pp
  ** For an explanation of ``soft-fill'', see the $$index_format documentation.
  */
  { "attach_save_charset_convert", DT_QUAD, R_NONE, {.l=OPT_ATTACH_SAVE_CHARCONV}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** When saving received text-type attachments, this quadoption
  ** prompts to convert the character set if the encoding of the
  ** attachment (or $$assumed_charset if none is specified) differs
  ** from $charset.
  */
  { "attach_save_dir",	DT_PATH, R_NONE, {.p=&AttachSaveDir}, {.p=0} },
  /*
  ** .pp
  ** The default directory to save attachments from the ``attachment'' menu.
  ** If it doesn't exist, Mutt will prompt to create the directory before
  ** saving.
  ** .pp
  ** If the path is invalid (e.g. not a directory, or cannot be
  ** chdir'ed to), Mutt will fall back to using the current directory.
  */
  { "attach_sep",	DT_STR,	 R_NONE, {.p=&AttachSep}, {.p="\n"} },
  /*
  ** .pp
  ** The separator to add between attachments when operating (saving,
  ** printing, piping, etc) on a list of tagged attachments.
  */
  { "attach_split",	DT_BOOL, R_NONE, {.l=OPTATTACHSPLIT}, {.l=1} },
  /*
  ** .pp
  ** If this variable is \fIunset\fP, when operating (saving, printing, piping,
  ** etc) on a list of tagged attachments, Mutt will concatenate the
  ** attachments and will operate on them as a single attachment. The
  ** $$attach_sep separator is added after each attachment. When \fIset\fP,
  ** Mutt will operate on the attachments one by one.
  */
  /* L10N:
     $attribution default value
  */
  { "attribution",	DT_STR|DT_L10N_STR, R_NONE, {.p=&Attribution}, {.p=N_("On %d, %n wrote:")} },
  /*
  ** .pp
  ** This is the string that will precede a message which has been included
  ** in a reply.  For a full listing of defined \fCprintf(3)\fP-like sequences see
  ** the section on $$index_format.
  */
  { "attribution_locale", DT_STR, R_NONE, {.p=&AttributionLocale}, {.p=0} },
  /*
  ** .pp
  ** The locale used by \fCstrftime(3)\fP to format dates in the
  ** $attribution string.  Legal values are the strings your system
  ** accepts for the locale environment variable \fC$$$LC_TIME\fP.
  ** .pp
  ** This variable is to allow the attribution date format to be
  ** customized by recipient or folder using hooks.  By default, Mutt
  ** will use your locale environment, so there is no need to set
  ** this except to override that default.
  */
  { "auto_subscribe",	DT_BOOL, R_NONE, {.l=OPTAUTOSUBSCRIBE}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt assumes the presence of a List-Post header
  ** means the recipient is subscribed to the list.  Unless the mailing list
  ** is in the ``unsubscribe'' or ``unlist'' lists, it will be added
  ** to the ``$subscribe'' list.  Parsing and checking these things slows
  ** header reading down, so this option is disabled by default.
  */
  { "auto_tag",		DT_BOOL, R_NONE, {.l=OPTAUTOTAG}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, functions in the \fIindex\fP menu which affect a message
  ** will be applied to all tagged messages (if there are any).  When
  ** unset, you must first use the \fC<tag-prefix>\fP function (bound to ``;''
  ** by default) to make the next function apply to all tagged messages.
  */
#ifdef USE_AUTOCRYPT
  { "autocrypt",	DT_BOOL, R_NONE, {.l=OPTAUTOCRYPT}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, enables autocrypt, which provides
  ** passive encryption protection with keys exchanged via headers.
  ** See ``$autocryptdoc'' for more details.
  ** (Autocrypt only)
  */
  { "autocrypt_acct_format", DT_STR, R_MENU, {.p=&AutocryptAcctFormat}, {.p="%4n %-30a %20p %10s"} },
  /*
  ** .pp
  ** This variable describes the format of the ``autocrypt account'' menu.
  ** The following \fCprintf(3)\fP-style sequences are understood
  ** .dl
  ** .dt %a  .dd email address
  ** .dt %k  .dd gpg keyid
  ** .dt %n  .dd current entry number
  ** .dt %p  .dd prefer-encrypt flag
  ** .dt %s  .dd status flag (active/inactive)
  ** .de
  ** .pp
  ** (Autocrypt only)
  */
  { "autocrypt_dir",	DT_PATH, R_NONE, {.p=&AutocryptDir}, {.p="~/.mutt/autocrypt"} },
  /*
  ** .pp
  ** This variable sets where autocrypt files are stored, including the GPG
  ** keyring and sqlite database.  See ``$autocryptdoc'' for more details.
  ** (Autocrypt only)
  */
  { "autocrypt_reply",	DT_BOOL, R_NONE, {.l=OPTAUTOCRYPTREPLY}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, replying to an autocrypt email automatically
  ** enables autocrypt in the reply.  You may want to unset this if you're using
  ** the same key for autocrypt as normal web-of-trust, so that autocrypt
  ** isn't forced on for all encrypted replies.
  ** (Autocrypt only)
  */
#endif
  { "autoedit",		DT_BOOL, R_NONE, {.l=OPTAUTOEDIT}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP along with $$edit_headers, Mutt will skip the initial
  ** send-menu (prompting for subject and recipients) and allow you to
  ** immediately begin editing the body of your
  ** message.  The send-menu may still be accessed once you have finished
  ** editing the body of your message.
  ** .pp
  ** .pp
  ** \fBNote:\fP when this option is \fIset\fP, you cannot use send-hooks that depend
  ** on the recipients when composing a new (non-reply) message, as the initial
  ** list of recipients is empty.
  ** .pp
  ** Also see $$fast_reply.
  */
  { "background_edit",  DT_BOOL, R_NONE, {.l=OPTBACKGROUNDEDIT}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will run $$editor in the background during
  ** message composition.  A landing page will display, waiting for
  ** the $$editor to exit.  The landing page may be exited, allowing
  ** perusal of the mailbox, or even for other messages to be
  ** composed.  Backgrounded sessions may be returned to via the
  ** \fC<background-compose-menu>\fP function.
  ** .pp
  ** For background editing to work properly, $$editor must be set to
  ** an editor that does not try to use the Mutt terminal: for example
  ** a graphical editor, or a script launching (and waiting for) the
  ** editor in another Gnu Screen window.
  ** .pp
  ** For more details, see ``$bgedit'' ("Background Editing" in the manual).
  */
  { "background_confirm_quit", DT_BOOL, R_NONE, {.l=OPTBACKGROUNDCONFIRMQUIT}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, if there are any background edit sessions, you
  ** will be prompted to confirm exiting Mutt, in addition to the
  ** $$quit prompt.
  */
  { "background_format", DT_STR, R_MENU, {.p=&BackgroundFormat}, {.p="%10S %7p %s"} },
  /*
  ** .pp
  ** This variable describes the format of the ``background compose''
  ** menu.  The following \fCprintf(3)\fP-style sequences are
  ** understood:
  ** .dl
  ** .dt %i .dd parent message id (for replies and forwarded messages)
  ** .dt %n .dd the running number on the menu
  ** .dt %p .dd pid of the $$editor process
  ** .dt %r .dd comma separated list of ``To:'' recipients
  ** .dt %R .dd comma separated list of ``Cc:'' recipients
  ** .dt %s .dd subject of the message
  ** .dt %S .dd status of the $$editor process: running/finished
  */
  { "beep",		DT_BOOL, R_NONE, {.l=OPTBEEP}, {.l=1} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, mutt will beep when an error occurs.
  */
  { "beep_new",		DT_BOOL, R_NONE, {.l=OPTBEEPNEW}, {.l=0} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, mutt will beep whenever it prints a message
  ** notifying you of new mail.  This is independent of the setting of the
  ** $$beep variable.
  */
  { "bounce",	DT_QUAD, R_NONE, {.l=OPT_BOUNCE}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** Controls whether you will be asked to confirm bouncing messages.
  ** If set to \fIyes\fP you don't get asked if you want to bounce a
  ** message. Setting this variable to \fIno\fP is not generally useful,
  ** and thus not recommended, because you are unable to bounce messages.
  */
  { "bounce_delivered", DT_BOOL, R_NONE, {.l=OPTBOUNCEDELIVERED}, {.l=1} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, mutt will include Delivered-To headers when
  ** bouncing messages.  Postfix users may wish to \fIunset\fP this variable.
  */
  { "braille_friendly", DT_BOOL, R_NONE, {.l=OPTBRAILLEFRIENDLY}, {.l=0} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, mutt will place the cursor at the beginning
  ** of the current line in menus, even when the $$arrow_cursor variable
  ** is \fIunset\fP, making it easier for blind persons using Braille displays to
  ** follow these menus.  The option is \fIunset\fP by default because many
  ** visual terminals don't permit making the cursor invisible.
  */
  { "browser_abbreviate_mailboxes", DT_BOOL, R_NONE, {.l=OPTBROWSERABBRMAILBOXES}, {.l=1} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, mutt will abbreviate mailbox
  ** names in the browser mailbox list, using '~' and '='
  ** shortcuts.
  ** .pp
  ** The default \fC"alpha"\fP setting of $$sort_browser uses
  ** locale-based sorting (using \fCstrcoll(3)\fP), which ignores some
  ** punctuation.  This can lead to some situations where the order
  ** doesn't make intuitive sense.  In those cases, it may be
  ** desirable to \fIunset\fP this variable.
  */
  { "browser_sticky_cursor", DT_BOOL, R_NONE, {.l=OPTBROWSERSTICKYCURSOR}, {.l=1} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, the browser will attempt to keep
  ** the cursor on the same mailbox when performing various functions.
  ** These include moving up a directory, toggling between mailboxes
  ** and directory listing, creating/renaming a mailbox, toggling
  ** subscribed mailboxes, and entering a new mask.
  */
#if defined(USE_SSL)
  { "certificate_file",	DT_PATH, R_NONE, {.p=&SslCertFile}, {.p="~/.mutt_certificates"} },
  /*
  ** .pp
  ** This variable specifies the file where the certificates you trust
  ** are saved. When an unknown certificate is encountered, you are asked
  ** if you accept it or not. If you accept it, the certificate can also
  ** be saved in this file and further connections are automatically
  ** accepted.
  ** .pp
  ** You can also manually add CA certificates in this file. Any server
  ** certificate that is signed with one of these CA certificates is
  ** also automatically accepted.
  ** .pp
  ** Example:
  ** .ts
  ** set certificate_file=~/.mutt/certificates
  ** .te
  ** .pp
  ** (OpenSSL and GnuTLS only)
  */
#endif
  { "change_folder_next", DT_BOOL, R_NONE, {.l=OPTCHANGEFOLDERNEXT}, {.l=0} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, the \fC<change-folder>\fP function
  ** mailbox suggestion will start at the next folder in your ``$mailboxes''
  ** list, instead of starting at the first folder in the list.
  */
  { "charset",		DT_STR,	 R_NONE, {.p=&Charset}, {.p=0} },
  /*
  ** .pp
  ** Character set your terminal uses to display and enter textual data.
  ** It is also the fallback for $$send_charset.
  ** .pp
  ** Upon startup Mutt tries to derive this value from environment variables
  ** such as \fC$$$LC_CTYPE\fP or \fC$$$LANG\fP.
  ** .pp
  ** \fBNote:\fP It should only be set in case Mutt isn't able to determine the
  ** character set used correctly.
  */
  { "check_mbox_size",	DT_BOOL, R_NONE, {.l=OPTCHECKMBOXSIZE}, {.l=0} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, mutt will use file size attribute instead of
  ** access time when checking for new mail in mbox and mmdf folders.
  ** .pp
  ** This variable is \fIunset\fP by default and should only be enabled when
  ** new mail detection for these folder types is unreliable or doesn't work.
  ** .pp
  ** Note that enabling this variable should happen before any ``$mailboxes''
  ** directives occur in configuration files regarding mbox or mmdf folders
  ** because mutt needs to determine the initial new mail status of such a
  ** mailbox by performing a fast mailbox scan when it is defined.
  ** Afterwards the new mail status is tracked by file size changes.
  */
  { "check_new",	DT_BOOL, R_NONE, {.l=OPTCHECKNEW}, {.l=1} },
  /*
  ** .pp
  ** \fBNote:\fP this option only affects \fImaildir\fP and \fIMH\fP style
  ** mailboxes.
  ** .pp
  ** When \fIset\fP, Mutt will check for new mail delivered while the
  ** mailbox is open.  Especially with MH mailboxes, this operation can
  ** take quite some time since it involves scanning the directory and
  ** checking each file to see if it has already been looked at.  If
  ** this variable is \fIunset\fP, no check for new mail is performed
  ** while the mailbox is open.
  */
  { "collapse_unread",	DT_BOOL, R_NONE, {.l=OPTCOLLAPSEUNREAD}, {.l=1} },
  /*
  ** .pp
  ** When \fIunset\fP, Mutt will not collapse a thread if it contains any
  ** unread messages.
  */
  { "compose_confirm_detach_first", DT_BOOL, R_NONE, {.l=OPTCOMPOSECONFIRMDETACH}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will prompt for confirmation when trying to
  ** use \fC<detach-file>\fP on the first entry in the compose menu.
  ** This is to help prevent irreversible loss of the typed message by
  ** accidentally hitting 'D' in the menu.
  ** .pp
  ** Note: Mutt only prompts for the first entry.  It doesn't keep
  ** track of which message is the typed message if the entries are
  ** reordered, or if the first entry was already deleted.
  */
  /* L10N:
     $compose_format default value
  */
  { "compose_format",	DT_STR|DT_L10N_STR, R_MENU, {.p=&ComposeFormat}, {.p=N_("-- Mutt: Compose  [Approx. msg size: %l   Atts: %a]%>-")} },
  /*
  ** .pp
  ** Controls the format of the status line displayed in the ``compose''
  ** menu.  This string is similar to $$status_format, but has its own
  ** set of \fCprintf(3)\fP-like sequences:
  ** .dl
  ** .dt %a .dd total number of attachments
  ** .dt %h .dd local hostname
  ** .dt %l .dd approximate size (in bytes) of the current message (see $formatstrings-size)
  ** .dt %v .dd Mutt version string
  ** .de
  ** .pp
  ** See the text describing the $$status_format option for more
  ** information on how to set $$compose_format.
  */
  { "config_charset",	DT_STR,  R_NONE, {.p=&ConfigCharset}, {.p=0} },
  /*
  ** .pp
  ** When defined, Mutt will recode commands in rc files from this
  ** encoding to the current character set as specified by $$charset
  ** and aliases written to $$alias_file from the current character set.
  ** .pp
  ** Please note that if setting $$charset it must be done before
  ** setting $$config_charset.
  ** .pp
  ** Recoding should be avoided as it may render unconvertable
  ** characters as question marks which can lead to undesired
  ** side effects (for example in regular expressions).
  */
  { "confirmappend",	DT_BOOL, R_NONE, {.l=OPTCONFIRMAPPEND}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will prompt for confirmation when appending messages to
  ** an existing mailbox.
  */
  { "confirmcreate",	DT_BOOL, R_NONE, {.l=OPTCONFIRMCREATE}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will prompt for confirmation when saving messages to a
  ** mailbox which does not yet exist before creating it.
  */
  { "connect_timeout",	DT_NUM,	R_NONE, {.p=&ConnectTimeout}, {.l=30} },
  /*
  ** .pp
  ** Causes Mutt to timeout a network connection (for IMAP, POP or SMTP) after this
  ** many seconds if the connection is not able to be established.  A negative
  ** value causes Mutt to wait indefinitely for the connection attempt to succeed.
  */
  { "content_type",	DT_STR, R_NONE, {.p=&ContentType}, {.p="text/plain"} },
  /*
  ** .pp
  ** Sets the default Content-Type for the body of newly composed messages.
  */
  { "copy",		DT_QUAD, R_NONE, {.l=OPT_COPY}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** This variable controls whether or not copies of your outgoing messages
  ** will be saved for later references.  Also see $$record,
  ** $$save_name, $$force_name and ``$fcc-hook''.
  */
  { "copy_decode_weed",	DT_BOOL, R_NONE, {.l=OPTCOPYDECODEWEED}, {.l=0} },
  /*
  ** .pp
  ** Controls whether Mutt will weed headers when invoking the
  ** \fC<decode-copy>\fP or \fC<decode-save>\fP functions.
  */
  { "count_alternatives", DT_BOOL, R_NONE, {.l=OPTCOUNTALTERNATIVES}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will recurse inside multipart/alternatives while
  ** performing attachment searching and counting (see $attachments).
  ** .pp
  ** Traditionally, multipart/alternative parts have simply represented
  ** different encodings of the main content of the email.  Unfortunately,
  ** some mail clients have started to place email attachments inside
  ** one of alternatives.  Setting this will allow Mutt to find
  ** and count matching attachments hidden there, and include them
  ** in the index via %X or through ~X pattern matching.
  */
  { "cursor_overlay", DT_BOOL, R_BOTH|R_SIDEBAR, {.l=OPTCURSOROVERLAY}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will overlay the indicator, tree,
  ** sidebar_highlight, and sidebar_indicator colors onto the currently
  ** selected line.  This will allow \fCdefault\fP colors in those
  ** to be overridden, and for attributes to be merged between
  ** the layers.
  */
  { "pgp_autoencrypt",		DT_SYN,  R_NONE, {.p="crypt_autoencrypt"}, {.p=0} },
  { "crypt_autoencrypt",	DT_BOOL, R_NONE, {.l=OPTCRYPTAUTOENCRYPT}, {.l=0} },
  /*
  ** .pp
  ** Setting this variable will cause Mutt to always attempt to PGP
  ** encrypt outgoing messages.  This is probably only useful in
  ** connection to the ``$send-hook'' command.  It can be overridden
  ** by use of the pgp menu, when encryption is not required or
  ** signing is requested as well.  If $$smime_is_default is \fIset\fP,
  ** then OpenSSL is used instead to create S/MIME messages and
  ** settings can be overridden by use of the smime menu instead.
  ** (Crypto only)
  */
  { "crypt_autopgp",	DT_BOOL, R_NONE, {.l=OPTCRYPTAUTOPGP}, {.l=1} },
  /*
  ** .pp
  ** This variable controls whether or not mutt may automatically enable
  ** PGP encryption/signing for messages.  See also $$crypt_autoencrypt,
  ** $$crypt_replyencrypt,
  ** $$crypt_autosign, $$crypt_replysign and $$smime_is_default.
  */
  { "pgp_autosign", 	DT_SYN,  R_NONE, {.p="crypt_autosign"}, {.p=0} },
  { "crypt_autosign",	DT_BOOL, R_NONE, {.l=OPTCRYPTAUTOSIGN}, {.l=0} },
  /*
  ** .pp
  ** Setting this variable will cause Mutt to always attempt to
  ** cryptographically sign outgoing messages.  This can be overridden
  ** by use of the pgp menu, when signing is not required or
  ** encryption is requested as well. If $$smime_is_default is \fIset\fP,
  ** then OpenSSL is used instead to create S/MIME messages and settings can
  ** be overridden by use of the smime menu instead of the pgp menu.
  ** (Crypto only)
  */
  { "crypt_autosmime",	DT_BOOL, R_NONE, {.l=OPTCRYPTAUTOSMIME}, {.l=1} },
  /*
  ** .pp
  ** This variable controls whether or not mutt may automatically enable
  ** S/MIME encryption/signing for messages. See also $$crypt_autoencrypt,
  ** $$crypt_replyencrypt,
  ** $$crypt_autosign, $$crypt_replysign and $$smime_is_default.
  */
  { "crypt_confirmhook",	DT_BOOL, R_NONE, {.l=OPTCRYPTCONFIRMHOOK}, {.l=1} },
  /*
  ** .pp
  ** If set, then you will be prompted for confirmation of keys when using
  ** the \fIcrypt-hook\fP command.  If unset, no such confirmation prompt will
  ** be presented.  This is generally considered unsafe, especially where
  ** typos are concerned.
  */
  { "crypt_opportunistic_encrypt", DT_BOOL, R_NONE, {.l=OPTCRYPTOPPORTUNISTICENCRYPT}, {.l=0} },
  /*
  ** .pp
  ** Setting this variable will cause Mutt to automatically enable and
  ** disable encryption, based on whether all message recipient keys
  ** can be located by Mutt.
  ** .pp
  ** When this option is enabled, Mutt will enable/disable encryption
  ** each time the TO, CC, and BCC lists are edited.  If
  ** $$edit_headers is set, Mutt will also do so each time the message
  ** is edited.
  ** .pp
  ** While this is set, encryption can't be manually enabled/disabled.
  ** The pgp or smime menus provide a selection to temporarily disable
  ** this option for the current message.
  ** .pp
  ** If $$crypt_autoencrypt or $$crypt_replyencrypt enable encryption for
  ** a message, this option will be disabled for that message.  It can
  ** be manually re-enabled in the pgp or smime menus.
  ** (Crypto only)
   */
  { "crypt_opportunistic_encrypt_strong_keys", DT_BOOL, R_NONE, {.l=OPTCRYPTOPPENCSTRONGKEYS}, {.l=0} },
  /*
  ** .pp
  ** When set, this modifies the behavior of $$crypt_opportunistic_encrypt
  ** to only search for "strong keys", that is, keys with full validity
  ** according to the web-of-trust algorithm.  A key with marginal or no
  ** validity will not enable opportunistic encryption.
  ** .pp
  ** For S/MIME, the behavior depends on the backend.  Classic S/MIME will
  ** filter for certificates with the 't' (trusted) flag in the .index file.
  ** The GPGME backend will use the same filters as with OpenPGP, and depends
  ** on GPGME's logic for assigning the GPGME_VALIDITY_FULL and
  ** GPGME_VALIDITY_ULTIMATE validity flag.
  */
  { "crypt_protected_headers_read", DT_BOOL, R_NONE, {.l=OPTCRYPTPROTHDRSREAD}, {.l=1} },
  /*
  ** .pp
  ** When set, Mutt will display protected headers in the pager,
  ** and will update the index and header cache with revised headers.
  **
  ** Protected headers are stored inside the encrypted or signed part of an
  ** an email, to prevent disclosure or tampering.
  ** For more information see https://github.com/autocrypt/protected-headers.
  ** Currently Mutt only supports the Subject header.
  ** .pp
  ** Encrypted messages using protected headers often substitute the exposed
  ** Subject header with a dummy value (see $$crypt_protected_headers_subject).
  ** Mutt will update its concept of the correct subject \fBafter\fP the
  ** message is opened, i.e. via the \fC<display-message>\fP function.
  ** If you reply to a message before opening it, Mutt will end up using
  ** the dummy Subject header, so be sure to open such a message first.
  ** (Crypto only)
   */
  { "crypt_protected_headers_save", DT_BOOL, R_NONE, {.l=OPTCRYPTPROTHDRSSAVE}, {.l=0} },
  /*
  ** .pp
  ** When $$crypt_protected_headers_read is set, and a message with a
  ** protected Subject is opened, Mutt will save the updated Subject
  ** into the header cache by default.  This allows searching/limiting
  ** based on the protected Subject header if the mailbox is
  ** re-opened, without having to re-open the message each time.
  ** However, for mbox/mh mailbox types, or if header caching is not
  ** set up, you would need to re-open the message each time the
  ** mailbox was reopened before you could see or search/limit on the
  ** protected subject again.
  ** .pp
  ** When this variable is set, Mutt additionally saves the protected
  ** Subject back \fBin the clear-text message headers\fP.  This
  ** provides better usability, but with the tradeoff of reduced
  ** security.  The protected Subject header, which may have
  ** previously been encrypted, is now stored in clear-text in the
  ** message headers.  Copying the message elsewhere, via Mutt or
  ** external tools, could expose this previously encrypted data.
  ** Please make sure you understand the consequences of this before
  ** you enable this variable.
  ** (Crypto only)
   */
  { "crypt_protected_headers_subject", DT_STR, R_NONE, {.p=&ProtHdrSubject}, {.p="..."} },
  /*
  ** .pp
  ** When $$crypt_protected_headers_write is set, and the message is marked
  ** for encryption, this will be substituted into the Subject field in the
  ** message headers.
  **
  ** To prevent a subject from being substituted, unset this variable, or set it
  ** to the empty string.
  ** (Crypto only)
   */
  { "crypt_protected_headers_write", DT_BOOL, R_NONE, {.l=OPTCRYPTPROTHDRSWRITE}, {.l=0} },
  /*
  ** .pp
  ** When set, Mutt will generate protected headers for signed and
  ** encrypted emails.
  **
  ** Protected headers are stored inside the encrypted or signed part of an
  ** an email, to prevent disclosure or tampering.
  ** For more information see https://github.com/autocrypt/protected-headers.
  **
  ** Currently Mutt only supports the Subject header.
  ** (Crypto only)
   */
  { "pgp_replyencrypt",		DT_SYN,  R_NONE, {.p="crypt_replyencrypt"}, {.p=0} },
  { "crypt_replyencrypt",	DT_BOOL, R_NONE, {.l=OPTCRYPTREPLYENCRYPT}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, automatically PGP or OpenSSL encrypt replies to messages which are
  ** encrypted.
  ** (Crypto only)
  */
  { "pgp_replysign",	DT_SYN, R_NONE, {.p="crypt_replysign"}, {.p=0} },
  { "crypt_replysign",	DT_BOOL, R_NONE, {.l=OPTCRYPTREPLYSIGN}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, automatically PGP or OpenSSL sign replies to messages which are
  ** signed.
  ** .pp
  ** \fBNote:\fP this does not work on messages that are encrypted
  ** \fIand\fP signed!
  ** (Crypto only)
  */
  { "pgp_replysignencrypted",   DT_SYN,  R_NONE, {.p="crypt_replysignencrypted"}, {.p=0} },
  { "crypt_replysignencrypted", DT_BOOL, R_NONE, {.l=OPTCRYPTREPLYSIGNENCRYPTED}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, automatically PGP or OpenSSL sign replies to messages
  ** which are encrypted. This makes sense in combination with
  ** $$crypt_replyencrypt, because it allows you to sign all
  ** messages which are automatically encrypted.  This works around
  ** the problem noted in $$crypt_replysign, that mutt is not able
  ** to find out whether an encrypted message is also signed.
  ** (Crypto only)
  */
  { "crypt_timestamp", DT_BOOL, R_NONE, {.l=OPTCRYPTTIMESTAMP}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will include a time stamp in the lines surrounding
  ** PGP or S/MIME output, so spoofing such lines is more difficult.
  ** If you are using colors to mark these lines, and rely on these,
  ** you may \fIunset\fP this setting.
  ** (Crypto only)
  */
  { "crypt_use_gpgme",  DT_BOOL, R_NONE, {.l=OPTCRYPTUSEGPGME}, {.l=0} },
  /*
  ** .pp
  ** This variable controls the use of the GPGME-enabled crypto backends.
  ** If it is \fIset\fP and Mutt was built with gpgme support, the gpgme code for
  ** S/MIME and PGP will be used instead of the classic code.  Note that
  ** you need to set this option in .muttrc; it won't have any effect when
  ** used interactively.
  ** .pp
  ** Note that the GPGME backend does not support creating old-style inline
  ** (traditional) PGP encrypted or signed messages (see $$pgp_autoinline).
  */
  { "crypt_use_pka", DT_BOOL, R_NONE, {.l=OPTCRYPTUSEPKA}, {.l=0} },
  /*
  ** .pp
  ** Controls whether mutt uses PKA
  ** (see http://www.g10code.de/docs/pka-intro.de.pdf) during signature
  ** verification (only supported by the GPGME backend).
  */
  { "pgp_verify_sig",   DT_SYN,  R_NONE, {.p="crypt_verify_sig"}, {.p=0} },
  { "crypt_verify_sig",	DT_QUAD, R_NONE, {.l=OPT_VERIFYSIG}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** If \fI``yes''\fP, always attempt to verify PGP or S/MIME signatures.
  ** If \fI``ask-*''\fP, ask whether or not to verify the signature.
  ** If \fI``no''\fP, never attempt to verify cryptographic signatures.
  ** (Crypto only)
  */
  { "date_format",	DT_STR,	 R_MENU, {.p=&DateFmt}, {.p="!%a, %b %d, %Y at %I:%M:%S%p %Z"} },
  /*
  ** .pp
  ** This variable controls the format of the date printed by the ``%d''
  ** sequence in $$index_format.  This is passed to the \fCstrftime(3)\fP
  ** function to process the date, see the man page for the proper syntax.
  ** .pp
  ** Unless the first character in the string is a bang (``!''), the month
  ** and week day names are expanded according to the locale.
  ** If the first character in the string is a
  ** bang, the bang is discarded, and the month and week day names in the
  ** rest of the string are expanded in the \fIC\fP locale (that is in US
  ** English).
  */
  { "default_hook",	DT_STR,	 R_NONE, {.p=&DefaultHook}, {.p="~f %s !~P | (~P ~C %s)"} },
  /*
  ** .pp
  ** This variable controls how ``$message-hook'', ``$reply-hook'', ``$send-hook'',
  ** ``$send2-hook'', ``$save-hook'', and ``$fcc-hook'' will
  ** be interpreted if they are specified with only a simple regexp,
  ** instead of a matching pattern.  The hooks are expanded when they are
  ** declared, so a hook will be interpreted according to the value of this
  ** variable at the time the hook is declared.
  ** .pp
  ** The default value matches
  ** if the message is either from a user matching the regular expression
  ** given, or if it is from you (if the from address matches
  ** ``$alternates'') and is to or cc'ed to a user matching the given
  ** regular expression.
  */
  { "delete",		DT_QUAD, R_NONE, {.l=OPT_DELETE}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** Controls whether or not messages are really deleted when closing or
  ** synchronizing a mailbox.  If set to \fIyes\fP, messages marked for
  ** deleting will automatically be purged without prompting.  If set to
  ** \fIno\fP, messages marked for deletion will be kept in the mailbox.
  ** .pp
  ** This option is ignored for maildir-style mailboxes when $$maildir_trash
  ** is set.
  */
  { "delete_untag",	DT_BOOL, R_NONE, {.l=OPTDELETEUNTAG}, {.l=1} },
  /*
  ** .pp
  ** If this option is \fIset\fP, mutt will untag messages when marking them
  ** for deletion.  This applies when you either explicitly delete a message,
  ** or when you save it to another folder.
  */
  { "digest_collapse",	DT_BOOL, R_NONE, {.l=OPTDIGESTCOLLAPSE}, {.l=1} },
  /*
  ** .pp
  ** If this option is \fIset\fP, mutt's received-attachments menu will not show the subparts of
  ** individual messages in a multipart/digest.  To see these subparts, press ``v'' on that menu.
  */
  { "display_filter",	DT_CMD_PATH, R_PAGER, {.p=&DisplayFilter}, {.p=0} },
  /*
  ** .pp
  ** When set, specifies a command used to filter messages.  When a message
  ** is viewed it is passed as standard input to $$display_filter, and the
  ** filtered message is read from the standard output.
  */
#if defined(DL_STANDALONE) && defined(USE_DOTLOCK)
  { "dotlock_program",  DT_CMD_PATH, R_NONE, {.p=&MuttDotlock}, {.p=BINDIR "/mutt_dotlock"} },
  /*
  ** .pp
  ** Contains the path of the \fCmutt_dotlock(1)\fP binary to be used by
  ** mutt.
  */
#endif
  { "dsn_notify",	DT_STR,	 R_NONE, {.p=&DsnNotify}, {.p=0} },
  /*
  ** .pp
  ** This variable sets the request for when notification is returned.  The
  ** string consists of a comma separated list (no spaces!) of one or more
  ** of the following: \fInever\fP, to never request notification,
  ** \fIfailure\fP, to request notification on transmission failure,
  ** \fIdelay\fP, to be notified of message delays, \fIsuccess\fP, to be
  ** notified of successful transmission.
  ** .pp
  ** Example:
  ** .ts
  ** set dsn_notify="failure,delay"
  ** .te
  ** .pp
  ** \fBNote:\fP when using $$sendmail for delivery, you should not enable
  ** this unless you are either using Sendmail 8.8.x or greater or a MTA
  ** providing a \fCsendmail(1)\fP-compatible interface supporting the \fC-N\fP option
  ** for DSN. For SMTP delivery, DSN support is auto-detected so that it
  ** depends on the server whether DSN will be used or not.
  */
  { "dsn_return",	DT_STR,	 R_NONE, {.p=&DsnReturn}, {.p=0} },
  /*
  ** .pp
  ** This variable controls how much of your message is returned in DSN
  ** messages.  It may be set to either \fIhdrs\fP to return just the
  ** message header, or \fIfull\fP to return the full message.
  ** .pp
  ** Example:
  ** .ts
  ** set dsn_return=hdrs
  ** .te
  ** .pp
  ** \fBNote:\fP when using $$sendmail for delivery, you should not enable
  ** this unless you are either using Sendmail 8.8.x or greater or a MTA
  ** providing a \fCsendmail(1)\fP-compatible interface supporting the \fC-R\fP option
  ** for DSN. For SMTP delivery, DSN support is auto-detected so that it
  ** depends on the server whether DSN will be used or not.
  */
  { "duplicate_threads",	DT_BOOL, R_RESORT|R_RESORT_INIT|R_INDEX, {.l=OPTDUPTHREADS}, {.l=1} },
  /*
  ** .pp
  ** This variable controls whether mutt, when $$sort is set to \fIthreads\fP, threads
  ** messages with the same Message-Id together.  If it is \fIset\fP, it will indicate
  ** that it thinks they are duplicates of each other with an equals sign
  ** in the thread tree.
  */
  { "edit_headers",	DT_BOOL, R_NONE, {.l=OPTEDITHDRS}, {.l=0} },
  /*
  ** .pp
  ** This option allows you to edit the header of your outgoing messages
  ** along with the body of your message.
  ** .pp
  ** Although the compose menu may have localized header labels, the
  ** labels passed to your editor will be standard RFC 2822 headers,
  ** (e.g. To:, Cc:, Subject:).  Headers added in your editor must
  ** also be RFC 2822 headers, or one of the pseudo headers listed in
  ** ``$edit-header''.  Mutt will not understand localized header
  ** labels, just as it would not when parsing an actual email.
  ** .pp
  ** \fBNote\fP that changes made to the References: and Date: headers are
  ** ignored for interoperability reasons.
  */
  { "edit_hdrs",	DT_SYN,  R_NONE, {.p="edit_headers"}, {.p=0} },
  /*
  */
  { "editor",		DT_CMD_PATH, R_NONE, {.p=&Editor}, {.p=0} },
  /*
  ** .pp
  ** This variable specifies which editor is used by mutt.
  ** It defaults to the value of the \fC$$$VISUAL\fP, or \fC$$$EDITOR\fP, environment
  ** variable, or to the string ``vi'' if neither of those are set.
  ** .pp
  ** The \fC$$editor\fP string may contain a \fI%s\fP escape, which will be replaced by the name
  ** of the file to be edited.  If the \fI%s\fP escape does not appear in \fC$$editor\fP, a
  ** space and the name to be edited are appended.
  ** .pp
  ** The resulting string is then executed by running
  ** .ts
  ** sh -c 'string'
  ** .te
  ** .pp
  ** where \fIstring\fP is the expansion of \fC$$editor\fP described above.
  */
  { "encode_from",	DT_BOOL, R_NONE, {.l=OPTENCODEFROM}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will quoted-printable encode messages when
  ** they contain the string ``From '' (note the trailing space) in the beginning of a line.
  ** This is useful to avoid the tampering certain mail delivery and transport
  ** agents tend to do with messages (in order to prevent tools from
  ** misinterpreting the line as a mbox message separator).
  */
#if defined(USE_SSL_OPENSSL)
  { "entropy_file",	DT_PATH, R_NONE, {.p=&SslEntropyFile}, {.p=0} },
  /*
  ** .pp
  ** The file which includes random data that is used to initialize SSL
  ** library functions. (OpenSSL only)
  */
#endif
  { "envelope_from_address", DT_ADDR, R_NONE, {.p=&EnvFrom}, {.p=0} },
  /*
  ** .pp
  ** Manually sets the \fIenvelope\fP sender for outgoing messages.
  ** This value is ignored if $$use_envelope_from is \fIunset\fP.
  */
  { "error_history",	DT_NUM,	 R_NONE, {.p=&ErrorHistSize}, {.l=30} },
  /*
  ** .pp
  ** This variable controls the size (in number of strings remembered)
  ** of the error messages displayed by mutt.  These can be shown with
  ** the \fC<error-history>\fP function.  The history is cleared each
  ** time this variable is set.
  */
  { "escape",		DT_STR,	 R_NONE, {.p=&EscChar}, {.p="~"} },
  /*
  ** .pp
  ** Escape character to use for functions in the built-in editor.
  */
  { "fast_reply",	DT_BOOL, R_NONE, {.l=OPTFASTREPLY}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, the initial prompt for recipients and subject are skipped
  ** when replying to messages, and the initial prompt for subject is
  ** skipped when forwarding messages.
  ** .pp
  ** \fBNote:\fP this variable has no effect when the $$autoedit
  ** variable is \fIset\fP.
  */
  { "fcc_attach",	DT_QUAD, R_NONE, {.l=OPT_FCCATTACH}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** This variable controls whether or not attachments on outgoing messages
  ** are saved along with the main body of your message.
  ** .pp
  ** Note: $$fcc_before_send forces the default (set) behavior of this option.
  */
  { "fcc_before_send",	DT_BOOL, R_NONE, {.l=OPTFCCBEFORESEND}, {.l=0} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, FCCs will occur before sending
  ** the message.  Before sending, the message cannot be manipulated,
  ** so it will be stored the exact same as sent:
  ** $$fcc_attach and $$fcc_clear will be ignored (using their default
  ** values).
  ** .pp
  ** When \fIunset\fP, the default, FCCs will occur after sending.
  ** Variables $$fcc_attach and $$fcc_clear will be respected, allowing
  ** it to be stored without attachments or encryption/signing if
  ** desired.
  */
  { "fcc_clear",	DT_BOOL, R_NONE, {.l=OPTFCCCLEAR}, {.l=0} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, FCCs will be stored unencrypted and
  ** unsigned, even when the actual message is encrypted and/or
  ** signed.
  ** .pp
  ** Note: $$fcc_before_send forces the default (unset) behavior of this option.
  ** (PGP only)
  ** .pp
  ** See also $$pgp_self_encrypt, $$smime_self_encrypt.
  */
  { "fcc_delimiter", DT_STR, R_NONE, {.p=&FccDelimiter}, {.p=0} },
  /*
  ** .pp
  ** When specified, this allows the ability to Fcc to more than one
  ** mailbox.  The fcc value will be split by this delimiter and Mutt
  ** will evaluate each part as a mailbox separately.
  ** .pp
  ** See $$record, ``$fcc-hook'', and ``$fcc-save-hook''.
  */
  { "flag_safe", DT_BOOL, R_NONE, {.l=OPTFLAGSAFE}, {.l=0} },
  /*
  ** .pp
  ** If set, flagged messages cannot be deleted.
  */
  { "folder",		DT_PATH, R_NONE, {.p=&Maildir}, {.p="~/Mail"} },
  /*
  ** .pp
  ** Specifies the default location of your mailboxes.  A ``+'' or ``='' at the
  ** beginning of a pathname will be expanded to the value of this
  ** variable.  Note that if you change this variable (from the default)
  ** value you need to make sure that the assignment occurs \fIbefore\fP
  ** you use ``+'' or ``='' for any other variables since expansion takes place
  ** when handling the ``$mailboxes'' command.
  */
  { "folder_format",	DT_STR,	 R_MENU, {.p=&FolderFormat}, {.p="%2C %t %N %F %2l %-8.8u %-8.8g %8s %d %f"} },
  /*
  ** .pp
  ** This variable allows you to customize the file browser display to your
  ** personal taste.  This string is similar to $$index_format, but has
  ** its own set of \fCprintf(3)\fP-like sequences:
  ** .dl
  ** .dt %C  .dd current file number
  ** .dt %d  .dd date/time folder was last modified
  ** .dt %D  .dd date/time folder was last modified using $$date_format.
  ** .dt %f  .dd filename (``/'' is appended to directory names,
  **             ``@'' to symbolic links and ``*'' to executable
  **             files)
  ** .dt %F  .dd file permissions
  ** .dt %g  .dd group name (or numeric gid, if missing)
  ** .dt %l  .dd number of hard links
  ** .dt %m  .dd number of messages in the mailbox *
  ** .dt %n  .dd number of unread messages in the mailbox *
  ** .dt %N  .dd N if mailbox has new mail, blank otherwise
  ** .dt %s  .dd size in bytes (see $formatstrings-size)
  ** .dt %t  .dd ``*'' if the file is tagged, blank otherwise
  ** .dt %u  .dd owner name (or numeric uid, if missing)
  ** .dt %>X .dd right justify the rest of the string and pad with character ``X''
  ** .dt %|X .dd pad to the end of the line with character ``X''
  ** .dt %*X .dd soft-fill with character ``X'' as pad
  ** .de
  ** .pp
  ** For an explanation of ``soft-fill'', see the $$index_format documentation.
  ** .pp
  ** * = can be optionally printed if nonzero
  ** .pp
  ** %m, %n, and %N only work for monitored mailboxes.
  ** %m requires $$mail_check_stats to be set.
  ** %n requires $$mail_check_stats to be set (except for IMAP mailboxes).
  */
  { "followup_to",	DT_BOOL, R_NONE, {.l=OPTFOLLOWUPTO}, {.l=1} },
  /*
  ** .pp
  ** Controls whether or not the ``Mail-Followup-To:'' header field is
  ** generated when sending mail.  When \fIset\fP, Mutt will generate this
  ** field when you are replying to a known mailing list, specified with
  ** the ``$subscribe'' or ``$lists'' commands.
  ** .pp
  ** This field has two purposes.  First, preventing you from
  ** receiving duplicate copies of replies to messages which you send
  ** to mailing lists, and second, ensuring that you do get a reply
  ** separately for any messages sent to known lists to which you are
  ** not subscribed.
  ** .pp
  ** The header will contain only the list's address
  ** for subscribed lists, and both the list address and your own
  ** email address for unsubscribed lists.  Without this header, a
  ** group reply to your message sent to a subscribed list will be
  ** sent to both the list and your address, resulting in two copies
  ** of the same email for you.
  */
  { "force_name",	DT_BOOL, R_NONE, {.l=OPTFORCENAME}, {.l=0} },
  /*
  ** .pp
  ** This variable is similar to $$save_name, except that Mutt will
  ** store a copy of your outgoing message by the username of the address
  ** you are sending to even if that mailbox does not exist.
  ** .pp
  ** Also see the $$record variable.
  */
  { "forward_attachments", DT_QUAD, R_NONE, {.l=OPT_FORWATTS}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** When forwarding inline (i.e. $$mime_forward \fIunset\fP or
  ** answered with ``no'' and $$forward_decode \fIset\fP), attachments
  ** which cannot be decoded in a reasonable manner will be attached
  ** to the newly composed message if this quadoption is \fIset\fP or
  ** answered with ``yes''.
  */
  /* L10N:
     $forward_attribution_intro default value
  */
  { "forward_attribution_intro", DT_STR|DT_L10N_STR, R_NONE, {.p=&ForwardAttrIntro}, {.p=N_("----- Forwarded message from %f -----")} },
  /*
  ** .pp
  ** This is the string that will precede a message which has been forwarded
  ** in the main body of a message (when $$mime_forward is unset).
  ** For a full listing of defined \fCprintf(3)\fP-like sequences see
  ** the section on $$index_format.  See also $$attribution_locale.
  */
  /* L10N:
     $forward_attribution_trailer default value
  */
  { "forward_attribution_trailer", DT_STR|DT_L10N_STR, R_NONE, {.p=&ForwardAttrTrailer}, {.p=N_("----- End forwarded message -----")} },
  /*
  ** .pp
  ** This is the string that will follow a message which has been forwarded
  ** in the main body of a message (when $$mime_forward is unset).
  ** For a full listing of defined \fCprintf(3)\fP-like sequences see
  ** the section on $$index_format.  See also $$attribution_locale.
  */
  { "forward_decode",	DT_BOOL, R_NONE, {.l=OPTFORWDECODE}, {.l=1} },
  /*
  ** .pp
  ** Controls the decoding of complex MIME messages into \fCtext/plain\fP when
  ** forwarding a message.  The message header is also RFC2047 decoded.
  ** This variable is only used, if $$mime_forward is \fIunset\fP,
  ** otherwise $$mime_forward_decode is used instead.
  */
  { "forw_decode",	DT_SYN,  R_NONE, {.p="forward_decode"}, {.p=0} },
  /*
  */
  { "forward_decrypt",	DT_QUAD, R_NONE, {.l=OPT_FORWDECRYPT}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** This quadoption controls the handling of encrypted messages when
  ** forwarding or attaching a message.  When set to or answered
  ** ``yes'', the outer layer of encryption is stripped off.
  ** .pp
  ** This variable is used if $$mime_forward is \fIset\fP and
  ** $$mime_forward_decode is \fIunset\fP.  It is also used when
  ** attaching a message via \fC<attach-message>\fP in the compose
  ** menu.  (PGP only)
  */
  { "forw_decrypt",	DT_SYN,  R_NONE, {.p="forward_decrypt"}, {.p=0} },
  /*
  */
  { "forward_edit",	DT_QUAD, R_NONE, {.l=OPT_FORWEDIT}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** This quadoption controls whether or not the user is automatically
  ** placed in the editor when forwarding messages.  For those who always want
  ** to forward with no modification, use a setting of ``no''.
  */
  { "forward_format",	DT_STR,	 R_NONE, {.p=&ForwFmt}, {.p="[%a: %s]"} },
  /*
  ** .pp
  ** This variable controls the default subject when forwarding a message.
  ** It uses the same format sequences as the $$index_format variable.
  */
  { "forw_format",	DT_SYN,  R_NONE, {.p="forward_format"}, {.p=0} },
  /*
  */
  { "forward_quote",	DT_BOOL, R_NONE, {.l=OPTFORWQUOTE}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, forwarded messages included in the main body of the
  ** message (when $$mime_forward is \fIunset\fP) will be quoted using
  ** $$indent_string.
  */
  { "forw_quote",	DT_SYN,  R_NONE, {.p="forward_quote"}, {.p=0} },
  /*
  */
  { "from",		DT_ADDR, R_NONE, {.p=&From}, {.p=0} },
  /*
  ** .pp
  ** When \fIset\fP, this variable contains a default from address.  It
  ** can be overridden using ``$my_hdr'' (including from a ``$send-hook'') and
  ** $$reverse_name.  This variable is ignored if $$use_from is \fIunset\fP.
  ** .pp
  ** This setting defaults to the contents of the environment variable \fC$$$EMAIL\fP.
  */
  { "gecos_mask",	DT_RX,	 R_NONE, {.p=&GecosMask}, {.p="^[^,]*"} },
  /*
  ** .pp
  ** A regular expression used by mutt to parse the GECOS field of a password
  ** entry when expanding the alias.  The default value
  ** will return the string up to the first ``,'' encountered.
  ** If the GECOS field contains a string like ``lastname, firstname'' then you
  ** should set it to ``\fC.*\fP''.
  ** .pp
  ** This can be useful if you see the following behavior: you address an e-mail
  ** to user ID ``stevef'' whose full name is ``Steve Franklin''.  If mutt expands
  ** ``stevef'' to ``"Franklin" stevef@foo.bar'' then you should set the $$gecos_mask to
  ** a regular expression that will match the whole name so mutt will expand
  ** ``Franklin'' to ``Franklin, Steve''.
  */
  { "hdr_format",	DT_SYN,  R_NONE, {.p="index_format"}, {.p=0} },
  /*
  */
  { "hdrs",		DT_BOOL, R_NONE, {.l=OPTHDRS}, {.l=1} },
  /*
  ** .pp
  ** When \fIunset\fP, the header fields normally added by the ``$my_hdr''
  ** command are not created.  This variable \fImust\fP be unset before
  ** composing a new message or replying in order to take effect.  If \fIset\fP,
  ** the user defined header fields are added to every new message.
  */
  { "header",		DT_BOOL, R_NONE, {.l=OPTHEADER}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, this variable causes Mutt to include the header
  ** of the message you are replying to into the edit buffer.
  ** The $$weed setting applies.
  */
#ifdef USE_HCACHE
  { "header_cache", DT_PATH, R_NONE, {.p=&HeaderCache}, {.p=0} },
  /*
  ** .pp
  ** This variable points to the header cache database.
  ** If pointing to a directory Mutt will contain a header cache
  ** database file per folder, if pointing to a file that file will
  ** be a single global header cache. By default it is \fIunset\fP so no header
  ** caching will be used.  If pointing to a directory, it must be
  ** created in advance.
  ** .pp
  ** Header caching can greatly improve speed when opening POP, IMAP
  ** MH or Maildir folders, see ``$caching'' for details.
  */
# if defined(HAVE_QDBM) || defined(HAVE_TC) || defined(HAVE_KC)
  { "header_cache_compress", DT_BOOL, R_NONE, {.l=OPTHCACHECOMPRESS}, {.l=1} },
  /*
  ** .pp
  ** When mutt is compiled with qdbm, tokyocabinet, or kyotocabinet as header
  ** cache backend, this option determines whether the database will be compressed.
  ** Compression results in database files roughly being one fifth
  ** of the usual diskspace, but the decompression can result in a
  ** slower opening of cached folder(s) which in general is still
  ** much faster than opening non header cached folders.
  */
# endif /* HAVE_QDBM */
# if defined(HAVE_GDBM) || defined(HAVE_DB4)
  { "header_cache_pagesize", DT_LNUM, R_NONE, {.p=&HeaderCachePageSize}, {.l=16384} },
  /*
  ** .pp
  ** When mutt is compiled with either gdbm or bdb4 as the header cache backend,
  ** this option changes the database page size.  Too large or too small
  ** values can waste space, memory, or CPU time. The default should be more
  ** or less optimal for most use cases.
  */
# endif /* HAVE_GDBM || HAVE_DB4 */
#endif /* USE_HCACHE */
  { "header_color_partial", DT_BOOL, R_PAGER_FLOW, {.l=OPTHEADERCOLORPARTIAL}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, color header regexps behave like color body regexps:
  ** color is applied to the exact text matched by the regexp.  When
  ** \fIunset\fP, color is applied to the entire header.
  ** .pp
  ** One use of this option might be to apply color to just the header labels.
  ** .pp
  ** See ``$color'' for more details.
  */
  { "help",		DT_BOOL, R_REFLOW, {.l=OPTHELP}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, help lines describing the bindings for the major functions
  ** provided by each menu are displayed on the first line of the screen.
  ** .pp
  ** \fBNote:\fP The binding will not be displayed correctly if the
  ** function is bound to a sequence rather than a single keystroke.  Also,
  ** the help line may not be updated if a binding is changed while Mutt is
  ** running.  Since this variable is primarily aimed at new users, neither
  ** of these should present a major problem.
  */
  { "hidden_host",	DT_BOOL, R_NONE, {.l=OPTHIDDENHOST}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will skip the host name part of $$hostname variable
  ** when adding the domain part to addresses.  This variable does not
  ** affect the generation of Message-IDs, and it will not lead to the
  ** cut-off of first-level domains.
  */
  { "hide_limited",	DT_BOOL, R_TREE|R_INDEX, {.l=OPTHIDELIMITED}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will not show the presence of messages that are hidden
  ** by limiting, in the thread tree.
  */
  { "hide_missing",	DT_BOOL, R_TREE|R_INDEX, {.l=OPTHIDEMISSING}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will not show the presence of missing messages in the
  ** thread tree.
  */
  { "hide_thread_subject", DT_BOOL, R_TREE|R_INDEX, {.l=OPTHIDETHREADSUBJECT}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will not show the subject of messages in the thread
  ** tree that have the same subject as their parent or closest previously
  ** displayed sibling.
  */
  { "hide_top_limited",	DT_BOOL, R_TREE|R_INDEX, {.l=OPTHIDETOPLIMITED}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will not show the presence of messages that are hidden
  ** by limiting, at the top of threads in the thread tree.  Note that when
  ** $$hide_limited is \fIset\fP, this option will have no effect.
  */
  { "hide_top_missing",	DT_BOOL, R_TREE|R_INDEX, {.l=OPTHIDETOPMISSING}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will not show the presence of missing messages at the
  ** top of threads in the thread tree.  Note that when $$hide_missing is
  ** \fIset\fP, this option will have no effect.
  */
  { "history",		DT_NUM,	 R_NONE, {.p=&HistSize}, {.l=10} },
  /*
  ** .pp
  ** This variable controls the size (in number of strings remembered) of
  ** the string history buffer per category. The buffer is cleared each time the
  ** variable is set.
  */
  { "history_file",     DT_PATH, R_NONE, {.p=&HistFile}, {.p="~/.mutthistory"} },
  /*
  ** .pp
  ** The file in which Mutt will save its history.
  ** .pp
  ** Also see $$save_history.
  */
  { "history_remove_dups", DT_BOOL, R_NONE, {.l=OPTHISTREMOVEDUPS}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, all of the string history will be scanned for duplicates
  ** when a new entry is added.  Duplicate entries in the $$history_file will
  ** also be removed when it is periodically compacted.
  */
  { "honor_disposition", DT_BOOL, R_NONE, {.l=OPTHONORDISP}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will not display attachments with a
  ** disposition of ``attachment'' inline even if it could
  ** render the part to plain text. These MIME parts can only
  ** be viewed from the attachment menu.
  ** .pp
  ** If \fIunset\fP, Mutt will render all MIME parts it can
  ** properly transform to plain text.
  */
  { "honor_followup_to", DT_QUAD, R_NONE, {.l=OPT_MFUPTO}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** This variable controls whether or not a Mail-Followup-To header is
  ** honored when group-replying to a message.
  */
  { "hostname",		DT_STR,	 R_NONE, {.p=&Fqdn}, {.p=0} },
  /*
  ** .pp
  ** Specifies the fully-qualified hostname of the system mutt is running on
  ** containing the host's name and the DNS domain it belongs to. It is used
  ** as the domain part (after ``@'') for local email addresses as well as
  ** Message-Id headers.
  ** .pp
  ** Its value is determined at startup as follows: the node's
  ** hostname is first determined by the \fCuname(3)\fP function.  The
  ** domain is then looked up using the \fCgethostname(2)\fP and
  ** \fCgetaddrinfo(3)\fP functions.  If those calls are unable to
  ** determine the domain, the full value returned by uname is used.
  ** Optionally, Mutt can be compiled with a fixed domain name in
  ** which case a detected one is not used.
  ** .pp
  ** Starting in Mutt 2.0, the operations described in the previous
  ** paragraph are performed after the muttrc is processed, instead of
  ** beforehand.  This way, if the DNS operations are creating delays
  ** at startup, you can avoid those by manually setting the value in
  ** your muttrc.
  ** .pp
  ** Also see $$use_domain and $$hidden_host.
  */
#if defined(HAVE_LIBIDN) || defined(HAVE_LIBIDN2)
  { "idn_decode",	DT_BOOL, R_MENU, {.l=OPTIDNDECODE}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will show you international domain names decoded.
  ** Note: You can use IDNs for addresses even if this is \fIunset\fP.
  ** This variable only affects decoding. (IDN only)
  */
  { "idn_encode",	DT_BOOL, R_MENU, {.l=OPTIDNENCODE}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will encode international domain names using
  ** IDN.  Unset this if your SMTP server can handle newer (RFC 6531)
  ** UTF-8 encoded domains. (IDN only)
  */
#endif /* defined(HAVE_LIBIDN) || defined(HAVE_LIBIDN2) */
  { "ignore_linear_white_space",    DT_BOOL, R_NONE, {.l=OPTIGNORELWS}, {.l=0} },
  /*
  ** .pp
  ** This option replaces linear-white-space between encoded-word
  ** and text to a single space to prevent the display of MIME-encoded
  ** ``Subject:'' field from being divided into multiple lines.
  */
  { "ignore_list_reply_to", DT_BOOL, R_NONE, {.l=OPTIGNORELISTREPLYTO}, {.l=0} },
  /*
  ** .pp
  ** Affects the behavior of the \fC<reply>\fP function when replying to
  ** messages from mailing lists (as defined by the ``$subscribe'' or
  ** ``$lists'' commands).  When \fIset\fP, if the ``Reply-To:'' field is
  ** set to the same value as the ``To:'' field, Mutt assumes that the
  ** ``Reply-To:'' field was set by the mailing list to automate responses
  ** to the list, and will ignore this field.  To direct a response to the
  ** mailing list when this option is \fIset\fP, use the \fC$<list-reply>\fP
  ** function; \fC<group-reply>\fP will reply to both the sender and the
  ** list.
  */
#ifdef USE_IMAP
  { "imap_authenticators", DT_STR, R_NONE, {.p=&ImapAuthenticators}, {.p=0} },
  /*
  ** .pp
  ** This is a colon-delimited list of authentication methods mutt may
  ** attempt to use to log in to an IMAP server, in the order mutt should
  ** try them.  Authentication methods are either ``login'' or the right
  ** side of an IMAP ``AUTH=xxx'' capability string, e.g. ``digest-md5'', ``gssapi''
  ** or ``cram-md5''. This option is case-insensitive. If it's
  ** \fIunset\fP (the default) mutt will try all available methods,
  ** in order from most-secure to least-secure.
  ** .pp
  ** Example:
  ** .ts
  ** set imap_authenticators="gssapi:cram-md5:login"
  ** .te
  ** .pp
  ** \fBNote:\fP Mutt will only fall back to other authentication methods if
  ** the previous methods are unavailable. If a method is available but
  ** authentication fails, mutt will not connect to the IMAP server.
  */
  { "imap_check_subscribed",  DT_BOOL, R_NONE, {.l=OPTIMAPCHECKSUBSCRIBED}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will fetch the set of subscribed folders from
  ** your server on connection, and add them to the set of mailboxes
  ** it polls for new mail just as if you had issued individual ``$mailboxes''
  ** commands.
  */
  { "imap_condstore",  DT_BOOL, R_NONE, {.l=OPTIMAPCONDSTORE}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will use the CONDSTORE extension (RFC 7162)
  ** if advertised by the server.  Mutt's current implementation is basic,
  ** used only for initial message fetching and flag updates.
  ** .pp
  ** For some IMAP servers, enabling this will slightly speed up
  ** downloading initial messages.  Unfortunately, Gmail is not one
  ** those, and displays worse performance when enabled.  Your
  ** mileage may vary.
  */
#ifdef USE_ZLIB
  { "imap_deflate",		DT_BOOL, R_NONE, {.l=OPTIMAPDEFLATE}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will use the COMPRESS=DEFLATE extension (RFC
  ** 4978) if advertised by the server.
  ** .pp
  ** In general a good compression efficiency can be achieved, which
  ** speeds up reading large mailboxes also on fairly good connections.
  */
#endif
  { "imap_delim_chars",		DT_STR, R_NONE, {.p=&ImapDelimChars}, {.p="/."} },
  /*
  ** .pp
  ** This contains the list of characters which you would like to treat
  ** as folder separators for displaying IMAP paths. In particular it
  ** helps in using the ``='' shortcut for your \fIfolder\fP variable.
  */
  { "imap_fetch_chunk_size",	DT_LNUM, R_NONE, {.p=&ImapFetchChunkSize}, {.l=0} },
  /*
  ** .pp
  ** When set to a value greater than 0, new headers will be
  ** downloaded in groups of this many headers per request.  If you
  ** have a very large mailbox, this might prevent a timeout and
  ** disconnect when opening the mailbox, by sending a FETCH per set
  ** of this many headers, instead of a single FETCH for all new
  ** headers.
  */
  { "imap_headers",	DT_STR, R_INDEX, {.p=&ImapHeaders}, {.p=0} },
  /*
  ** .pp
  ** Mutt requests these header fields in addition to the default headers
  ** (``Date:'', ``From:'', ``Sender:'', ``Subject:'', ``To:'', ``Cc:'', ``Message-Id:'',
  ** ``References:'', ``Content-Type:'', ``Content-Description:'', ``In-Reply-To:'',
  ** ``Reply-To:'', ``Lines:'', ``List-Post:'', ``X-Label:'') from IMAP
  ** servers before displaying the index menu. You may want to add more
  ** headers for spam detection.
  ** .pp
  ** \fBNote:\fP This is a space separated list, items should be uppercase
  ** and not contain the colon, e.g. ``X-BOGOSITY X-SPAM-STATUS'' for the
  ** ``X-Bogosity:'' and ``X-Spam-Status:'' header fields.
  */
  { "imap_idle",                DT_BOOL, R_NONE, {.l=OPTIMAPIDLE}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will attempt to use the IMAP IDLE extension
  ** to check for new mail in the current mailbox. Some servers
  ** (dovecot was the inspiration for this option) react badly
  ** to mutt's implementation. If your connection seems to freeze
  ** up periodically, try unsetting this.
  */
  { "imap_keepalive",           DT_NUM,  R_NONE, {.p=&ImapKeepalive}, {.l=300} },
  /*
  ** .pp
  ** This variable specifies the maximum amount of time in seconds that mutt
  ** will wait before polling open IMAP connections, to prevent the server
  ** from closing them before mutt has finished with them. The default is
  ** well within the RFC-specified minimum amount of time (30 minutes) before
  ** a server is allowed to do this, but in practice the RFC does get
  ** violated every now and then. Reduce this number if you find yourself
  ** getting disconnected from your IMAP server due to inactivity.
  */
  { "imap_list_subscribed",	DT_BOOL, R_NONE, {.l=OPTIMAPLSUB}, {.l=0} },
  /*
  ** .pp
  ** This variable configures whether IMAP folder browsing will look for
  ** only subscribed folders or all folders.  This can be toggled in the
  ** IMAP browser with the \fC<toggle-subscribed>\fP function.
  */
  { "imap_login",	DT_STR,  R_NONE, {.p=&ImapLogin}, {.p=0} },
  /*
  ** .pp
  ** Your login name on the IMAP server.
  ** .pp
  ** This variable defaults to the value of $$imap_user.
  */
  { "imap_oauth_refresh_command", DT_STR, R_NONE, {.p=&ImapOauthRefreshCmd}, {.p=0} },
  /*
  ** .pp
  ** The command to run to generate an OAUTH refresh token for
  ** authorizing your connection to your IMAP server.  This command will be
  ** run on every connection attempt that uses the OAUTHBEARER authentication
  ** mechanism.  See ``$oauth'' for details.
  */
  { "imap_pass", 	DT_STR,  R_NONE, {.p=&ImapPass}, {.p=0} },
  /*
  ** .pp
  ** Specifies the password for your IMAP account.  If \fIunset\fP, Mutt will
  ** prompt you for your password when you invoke the \fC<imap-fetch-mail>\fP function
  ** or try to open an IMAP folder.
  ** .pp
  ** \fBWarning\fP: you should only use this option when you are on a
  ** fairly secure machine, because the superuser can read your muttrc even
  ** if you are the only one who can read the file.
  */
  { "imap_passive",		DT_BOOL, R_NONE, {.l=OPTIMAPPASSIVE}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will not open new IMAP connections to check for new
  ** mail.  Mutt will only check for new mail over existing IMAP
  ** connections.  This is useful if you don't want to be prompted for
  ** user/password pairs on mutt invocation, or if opening the connection
  ** is slow.
  */
  { "imap_peek", DT_BOOL, R_NONE, {.l=OPTIMAPPEEK}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will avoid implicitly marking your mail as read whenever
  ** you fetch a message from the server. This is generally a good thing,
  ** but can make closing an IMAP folder somewhat slower. This option
  ** exists to appease speed freaks.
  */
  { "imap_pipeline_depth", DT_NUM,  R_NONE, {.p=&ImapPipelineDepth}, {.l=15} },
  /*
  ** .pp
  ** Controls the number of IMAP commands that may be queued up before they
  ** are sent to the server. A deeper pipeline reduces the amount of time
  ** mutt must wait for the server, and can make IMAP servers feel much
  ** more responsive. But not all servers correctly handle pipelined commands,
  ** so if you have problems you might want to try setting this variable to 0.
  ** .pp
  ** \fBNote:\fP Changes to this variable have no effect on open connections.
  */
  { "imap_poll_timeout", DT_NUM,  R_NONE, {.p=&ImapPollTimeout}, {.l=15} },
  /*
  ** .pp
  ** This variable specifies the maximum amount of time in seconds
  ** that mutt will wait for a response when polling IMAP connections
  ** for new mail, before timing out and closing the connection.  Set
  ** to 0 to disable timing out.
  */
  { "imap_qresync",  DT_BOOL, R_NONE, {.l=OPTIMAPQRESYNC}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will use the QRESYNC extension (RFC 7162)
  ** if advertised by the server.  Mutt's current implementation is basic,
  ** used only for initial message fetching and flag updates.
  ** .pp
  ** Note: this feature is currently experimental.  If you experience
  ** strange behavior, such as duplicate or missing messages please
  ** file a bug report to let us know.
  */
  { "imap_servernoise",		DT_BOOL, R_NONE, {.l=OPTIMAPSERVERNOISE}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will display warning messages from the IMAP
  ** server as error messages. Since these messages are often
  ** harmless, or generated due to configuration problems on the
  ** server which are out of the users' hands, you may wish to suppress
  ** them at some point.
  */
  { "imap_user",	DT_STR,  R_NONE, {.p=&ImapUser}, {.p=0} },
  /*
  ** .pp
  ** The name of the user whose mail you intend to access on the IMAP
  ** server.
  ** .pp
  ** This variable defaults to your user name on the local machine.
  */
#endif
  { "implicit_autoview", DT_BOOL,R_NONE, {.l=OPTIMPLICITAUTOVIEW}, {.l=0} },
  /*
  ** .pp
  ** If set to ``yes'', mutt will look for a mailcap entry with the
  ** ``\fCcopiousoutput\fP'' flag set for \fIevery\fP MIME attachment it doesn't have
  ** an internal viewer defined for.  If such an entry is found, mutt will
  ** use the viewer defined in that entry to convert the body part to text
  ** form.
  */
  { "include",		DT_QUAD, R_NONE, {.l=OPT_INCLUDE}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** Controls whether or not a copy of the message(s) you are replying to
  ** is included in your reply.
  */
  { "include_encrypted",	DT_BOOL, R_NONE, {.l=OPTINCLUDEENCRYPTED}, {.l=0} },
  /*
  ** .pp
  ** Controls whether or not Mutt includes separately encrypted attachment
  ** contents when replying.
  ** .pp
  ** This variable was added to prevent accidental exposure of encrypted
  ** contents when replying to an attacker.  If a previously encrypted message
  ** were attached by the attacker, they could trick an unwary recipient into
  ** decrypting and including the message in their reply.
  */
  { "include_onlyfirst",	DT_BOOL, R_NONE, {.l=OPTINCLUDEONLYFIRST}, {.l=0} },
  /*
  ** .pp
  ** Controls whether or not Mutt includes only the first attachment
  ** of the message you are replying.
  */
  { "indent_string",	DT_STR,	 R_NONE, {.p=&Prefix}, {.p="> "} },
  /*
  ** .pp
  ** Specifies the string to prepend to each line of text quoted in a
  ** message to which you are replying.  You are strongly encouraged not to
  ** change this value, as it tends to agitate the more fanatical netizens.
  ** .pp
  ** The value of this option is ignored if $$text_flowed is set, because
  ** the quoting mechanism is strictly defined for format=flowed.
  ** .pp
  ** This option is a format string, please see the description of
  ** $$index_format for supported \fCprintf(3)\fP-style sequences.
  */
  { "indent_str",	DT_SYN,  R_NONE, {.p="indent_string"}, {.p=0} },
  /*
  */
  { "index_format",	DT_STR,	 R_BOTH, {.p=&HdrFmt}, {.p="%4C %Z %{%b %d} %-15.15L (%?l?%4l&%4c?) %s"} },
  /*
  ** .pp
  ** This variable allows you to customize the message index display to
  ** your personal taste.
  ** .pp
  ** ``Format strings'' are similar to the strings used in the C
  ** function \fCprintf(3)\fP to format output (see the man page for more details).
  ** For an explanation of the %? construct, see the $$status_format description.
  ** The following sequences are defined in Mutt:
  ** .dl
  ** .dt %a .dd address of the author
  ** .dt %A .dd reply-to address (if present; otherwise: address of author)
  ** .dt %b .dd filename of the original message folder (think mailbox)
  ** .dt %B .dd the list to which the letter was sent, or else the folder name (%b).
  ** .dt %c .dd number of characters (bytes) in the message (see $formatstrings-size)
  ** .dt %C .dd current message number
  ** .dt %d .dd date and time of the message in the format specified by
  **            $$date_format converted to sender's time zone
  ** .dt %D .dd date and time of the message in the format specified by
  **            $$date_format converted to the local time zone
  ** .dt %e .dd current message number in thread
  ** .dt %E .dd number of messages in current thread
  ** .dt %f .dd sender (address + real name), either From: or Return-Path:
  ** .dt %F .dd author name, or recipient name if the message is from you
  ** .dt %H .dd spam attribute(s) of this message
  ** .dt %i .dd message-id of the current message
  ** .dt %l .dd number of lines in the unprocessed message (may not work with
  **            maildir, mh, and IMAP folders)
  ** .dt %L .dd If an address in the ``To:'' or ``Cc:'' header field matches an address
  **            defined by the users ``$subscribe'' command, this displays
  **            "To <list-name>", otherwise the same as %F.
  ** .dt %m .dd total number of message in the mailbox
  ** .dt %M .dd number of hidden messages if the thread is collapsed.
  ** .dt %N .dd message score
  ** .dt %n .dd author's real name (or address if missing)
  ** .dt %O .dd original save folder where mutt would formerly have
  **            stashed the message: list name or recipient name
  **            if not sent to a list
  ** .dt %P .dd progress indicator for the built-in pager (how much of the file has been displayed)
  ** .dt %r .dd comma separated list of ``To:'' recipients
  ** .dt %R .dd comma separated list of ``Cc:'' recipients
  ** .dt %s .dd subject of the message
  ** .dt %S .dd single character status of the message (``N''/``O''/``D''/``d''/``!''/``r''/``\(as'')
  ** .dt %t .dd ``To:'' field (recipients)
  ** .dt %T .dd the appropriate character from the $$to_chars string
  ** .dt %u .dd user (login) name of the author
  ** .dt %v .dd first name of the author, or the recipient if the message is from you
  ** .dt %X .dd number of attachments
  **            (please see the ``$attachments'' section for possible speed effects)
  ** .dt %y .dd ``X-Label:'' field, if present
  ** .dt %Y .dd ``X-Label:'' field, if present, and \fI(1)\fP not at part of a thread tree,
  **            \fI(2)\fP at the top of a thread, or \fI(3)\fP ``X-Label:'' is different from
  **            preceding message's ``X-Label:''.
  ** .dt %Z .dd a three character set of message status flags.
  **            the first character is new/read/replied flags (``n''/``o''/``r''/``O''/``N'').
  **            the second is deleted or encryption flags (``D''/``d''/``S''/``P''/``s''/``K'').
  **            the third is either tagged/flagged (``\(as''/``!''), or one of the characters
  **            listed in $$to_chars.
  ** .dt %@name@ .dd insert and evaluate format-string from the matching
  **                 ``$index-format-hook'' command
  ** .dt %{fmt} .dd the date and time of the message is converted to sender's
  **                time zone, and ``fmt'' is expanded by the library function
  **                \fCstrftime(3)\fP; a leading bang disables locales
  ** .dt %[fmt] .dd the date and time of the message is converted to the local
  **                time zone, and ``fmt'' is expanded by the library function
  **                \fCstrftime(3)\fP; a leading bang disables locales
  ** .dt %(fmt) .dd the local date and time when the message was received.
  **                ``fmt'' is expanded by the library function \fCstrftime(3)\fP;
  **                a leading bang disables locales
  ** .dt %<fmt> .dd the current local time. ``fmt'' is expanded by the library
  **                function \fCstrftime(3)\fP; a leading bang disables locales.
  ** .dt %>X    .dd right justify the rest of the string and pad with character ``X''
  ** .dt %|X    .dd pad to the end of the line with character ``X''
  ** .dt %*X    .dd soft-fill with character ``X'' as pad
  ** .de
  ** .pp
  ** Note that for mbox/mmdf, ``%l'' applies to the unprocessed message, and
  ** for maildir/mh, the value comes from the ``Lines:'' header field when
  ** present (the meaning is normally the same). Thus the value depends on
  ** the encodings used in the different parts of the message and has little
  ** meaning in practice.
  ** .pp
  ** ``Soft-fill'' deserves some explanation: Normal right-justification
  ** will print everything to the left of the ``%>'', displaying padding and
  ** whatever lies to the right only if there's room. By contrast,
  ** soft-fill gives priority to the right-hand side, guaranteeing space
  ** to display it and showing padding only if there's still room. If
  ** necessary, soft-fill will eat text leftwards to make room for
  ** rightward text.
  ** .pp
  ** Note that these expandos are supported in
  ** ``$save-hook'', ``$fcc-hook'', ``$fcc-save-hook'', and
  ** ``$index-format-hook''.
  ** .pp
  ** They are also supported in the configuration variables $$attribution,
  ** $$forward_attribution_intro, $$forward_attribution_trailer,
  ** $$forward_format, $$indent_string, $$message_format, $$pager_format,
  ** and $$post_indent_string.
  */
  { "ispell",		DT_CMD_PATH, R_NONE, {.p=&Ispell}, {.p=ISPELL} },
  /*
  ** .pp
  ** How to invoke ispell (GNU's spell-checking software).
  */
  { "keep_flagged", DT_BOOL, R_NONE, {.l=OPTKEEPFLAGGED}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, read messages marked as flagged will not be moved
  ** from your spool mailbox to your $$mbox mailbox, or as a result of
  ** a ``$mbox-hook'' command.
  */
  { "local_date_header", DT_BOOL, R_NONE, {.l=OPTLOCALDATEHEADER}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, the date in the Date header of emails that you send will be in
  ** your local timezone. If unset a UTC date will be used instead to avoid
  ** leaking information about your current location.
  */
  { "mail_check",	DT_NUM,  R_NONE, {.p=&BuffyTimeout}, {.l=5} },
  /*
  ** .pp
  ** This variable configures how often (in seconds) mutt should look for
  ** new mail. Also see the $$timeout variable.
  */
  { "mail_check_recent",DT_BOOL, R_NONE, {.l=OPTMAILCHECKRECENT}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will only notify you about new mail that has been received
  ** since the last time you opened the mailbox.  When \fIunset\fP, Mutt will notify you
  ** if any new mail exists in the mailbox, regardless of whether you have visited it
  ** recently.
  */
  { "mail_check_stats", DT_BOOL, R_NONE, {.l=OPTMAILCHECKSTATS}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will periodically calculate message
  ** statistics of a mailbox while polling for new mail.  It will
  ** check for unread, flagged, and total message counts.
  ** (Note: IMAP mailboxes only support unread and total counts).
  ** .pp
  ** Because this operation is more performance intensive, it defaults
  ** to \fIunset\fP, and has a separate option,
  ** $$mail_check_stats_interval, to control how often to update these
  ** counts.
  ** .pp
  ** Message statistics can also be explicitly calculated by invoking the
  ** \fC<check-stats>\fP
  ** function.
  */
  { "mail_check_stats_interval", DT_NUM, R_NONE, {.p=&BuffyCheckStatsInterval}, {.l=60} },
  /*
  ** .pp
  ** When $$mail_check_stats is \fIset\fP, this variable configures
  ** how often (in seconds) mutt will update message counts.
  */
  { "mailcap_path",	DT_STR,	 R_NONE, {.p=&MailcapPath}, {.p=0} },
  /*
  ** .pp
  ** This variable specifies which files to consult when attempting to
  ** display MIME bodies not directly supported by Mutt.  The default value
  ** is generated during startup: see the ``$mailcap'' section of the manual.
  */
  { "mailcap_sanitize",	DT_BOOL, R_NONE, {.l=OPTMAILCAPSANITIZE}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will restrict possible characters in mailcap % expandos
  ** to a well-defined set of safe characters.  This is the safe setting,
  ** but we are not sure it doesn't break some more advanced MIME stuff.
  ** .pp
  ** \fBDON'T CHANGE THIS SETTING UNLESS YOU ARE REALLY SURE WHAT YOU ARE
  ** DOING!\fP
  */
#ifdef USE_HCACHE
  { "maildir_header_cache_verify", DT_BOOL, R_NONE, {.l=OPTHCACHEVERIFY}, {.l=1} },
  /*
  ** .pp
  ** Check for Maildir unaware programs other than mutt having modified maildir
  ** files when the header cache is in use.  This incurs one \fCstat(2)\fP per
  ** message every time the folder is opened (which can be very slow for NFS
  ** folders).
  */
#endif
  { "maildir_trash", DT_BOOL, R_BOTH, {.l=OPTMAILDIRTRASH}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, messages marked as deleted will be saved with the maildir
  ** trashed flag instead of unlinked.  \fBNote:\fP this only applies
  ** to maildir-style mailboxes.  Setting it will have no effect on other
  ** mailbox types.
  */
  { "maildir_check_cur", DT_BOOL, R_NONE, {.l=OPTMAILDIRCHECKCUR}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will poll both the new and cur directories of
  ** a maildir folder for new messages.  This might be useful if other
  ** programs interacting with the folder (e.g. dovecot) are moving new
  ** messages to the cur directory.  Note that setting this option may
  ** slow down polling for new messages in large folders, since mutt has
  ** to scan all cur messages.
  */
  { "mark_macro_prefix",DT_STR, R_NONE, {.p=&MarkMacroPrefix}, {.p="'"} },
  /*
  ** .pp
  ** Prefix for macros created using mark-message.  A new macro
  ** automatically generated with \fI<mark-message>a\fP will be composed
  ** from this prefix and the letter \fIa\fP.
  */
  { "mark_old",		DT_BOOL, R_BOTH, {.l=OPTMARKOLD}, {.l=1} },
  /*
  ** .pp
  ** Controls whether or not mutt marks \fInew\fP \fBunread\fP
  ** messages as \fIold\fP if you exit a mailbox without reading them.
  ** With this option \fIset\fP, the next time you start mutt, the messages
  ** will show up with an ``O'' next to them in the index menu,
  ** indicating that they are old.
  */
  { "markers",		DT_BOOL, R_PAGER_FLOW, {.l=OPTMARKERS}, {.l=1} },
  /*
  ** .pp
  ** Controls the display of wrapped lines in the internal pager. If set, a
  ** ``+'' marker is displayed at the beginning of wrapped lines.
  ** .pp
  ** Also see the $$smart_wrap variable.
  */
  { "mask",		DT_RX,	 R_NONE, {.p=&Mask}, {.p="!^\\.[^.]"} },
  /*
  ** .pp
  ** A regular expression used in the file browser, optionally preceded by
  ** the \fInot\fP operator ``!''.  Only files whose names match this mask
  ** will be shown. The match is always case-sensitive.
  */
  { "mbox",		DT_PATH, R_BOTH, {.p=&Inbox}, {.p="~/mbox"} },
  /*
  ** .pp
  ** This specifies the folder into which read mail in your $$spoolfile
  ** folder will be appended.
  ** .pp
  ** Also see the $$move variable.
  */
  { "mbox_type",	DT_MAGIC,R_NONE, {.p=&DefaultMagic}, {.l=MUTT_MBOX} },
  /*
  ** .pp
  ** The default mailbox type used when creating new folders. May be any of
  ** ``mbox'', ``MMDF'', ``MH'' and ``Maildir''. This is overridden by the
  ** \fC-m\fP command-line option.
  */
  { "menu_context",	DT_NUM,  R_NONE, {.p=&MenuContext}, {.l=0} },
  /*
  ** .pp
  ** This variable controls the number of lines of context that are given
  ** when scrolling through menus. (Similar to $$pager_context.)
  */
  { "menu_move_off",	DT_BOOL, R_NONE, {.l=OPTMENUMOVEOFF}, {.l=1} },
  /*
  ** .pp
  ** When \fIunset\fP, the bottom entry of menus will never scroll up past
  ** the bottom of the screen, unless there are less entries than lines.
  ** When \fIset\fP, the bottom entry may move off the bottom.
  */
  { "menu_scroll",	DT_BOOL, R_NONE, {.l=OPTMENUSCROLL}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, menus will be scrolled up or down one line when you
  ** attempt to move across a screen boundary.  If \fIunset\fP, the screen
  ** is cleared and the next or previous page of the menu is displayed
  ** (useful for slow links to avoid many redraws).
  */
#if defined(USE_IMAP) || defined(USE_POP)
  { "message_cache_clean", DT_BOOL, R_NONE, {.l=OPTMESSAGECACHECLEAN}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will clean out obsolete entries from the message cache when
  ** the mailbox is synchronized. You probably only want to set it
  ** every once in a while, since it can be a little slow
  ** (especially for large folders).
  */
  { "message_cachedir",	DT_PATH,	R_NONE,	{.p=&MessageCachedir}, {.p=0} },
  /*
  ** .pp
  ** Set this to a directory and mutt will cache copies of messages from
  ** your IMAP and POP servers here. You are free to remove entries at any
  ** time.
  ** .pp
  ** When setting this variable to a directory, mutt needs to fetch every
  ** remote message only once and can perform regular expression searches
  ** as fast as for local folders.
  ** .pp
  ** Also see the $$message_cache_clean variable.
  */
#endif
  { "message_format",	DT_STR,	 R_NONE, {.p=&MsgFmt}, {.p="%s"} },
  /*
  ** .pp
  ** This is the string displayed in the ``attachment'' menu for
  ** attachments of type \fCmessage/rfc822\fP.  For a full listing of defined
  ** \fCprintf(3)\fP-like sequences see the section on $$index_format.
  */
  { "msg_format",	DT_SYN,  R_NONE, {.p="message_format"}, {.p=0} },
  /*
  */
  { "message_id_format", DT_STR, R_NONE, {.p=&MessageIdFormat}, {.p="<%z@%f>"} },
  /*
  ** .pp
  ** This variable describes the format of the Message-ID generated
  ** when sending messages.  Mutt 2.0 introduced a more compact
  ** format, but this variable allows the ability to choose your own
  ** format.  The value may end in ``|'' to invoke an external filter.
  ** See $formatstrings-filters.
  ** .pp
  ** Please note that the Message-ID value follows a strict syntax,
  ** and you are responsible for ensuring correctness if you change
  ** this from the default.  In particular, the value must follow the
  ** syntax in RFC 5322: ``\fC"<" id-left "@" id-right ">"\fP''.  No
  ** spaces are allowed, and \fCid-left\fP should follow the
  ** dot-atom-text syntax in the RFC.  The \fCid-right\fP should
  ** generally be left at %f.
  ** .pp
  ** The old Message-ID format can be used by setting this to:
  ** ``\fC<%Y%02m%02d%02H%02M%02S.G%c%p@%f>\fP''
  ** .pp
  ** The following \fCprintf(3)\fP-style sequences are understood:
  ** .dl
  ** .dt %c .dd step counter looping from ``A'' to ``Z''
  ** .dt %d .dd current day of the month (GMT)
  ** .dt %f .dd $$hostname
  ** .dt %H .dd current hour using a 24-hour clock (GMT)
  ** .dt %m .dd current month number (GMT)
  ** .dt %M .dd current minute of the hour (GMT)
  ** .dt %p .dd pid of the running mutt process
  ** .dt %r .dd 3 bytes of pseudorandom data encoded in Base64
  ** .dt %S .dd current second of the minute (GMT)
  ** .dt %x .dd 1 byte of pseudorandom data hex encoded (example: '1b')
  ** .dt %Y .dd current year using 4 digits (GMT)
  ** .dt %z .dd 4 byte timestamp + 8 bytes of pseudorandom data encoded in Base64
  */
  { "meta_key",		DT_BOOL, R_NONE, {.l=OPTMETAKEY}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, forces Mutt to interpret keystrokes with the high bit (bit 8)
  ** set as if the user had pressed the Esc key and whatever key remains
  ** after having the high bit removed.  For example, if the key pressed
  ** has an ASCII value of \fC0xf8\fP, then this is treated as if the user had
  ** pressed Esc then ``x''.  This is because the result of removing the
  ** high bit from \fC0xf8\fP is \fC0x78\fP, which is the ASCII character
  ** ``x''.
  */
  { "metoo",		DT_BOOL, R_NONE, {.l=OPTMETOO}, {.l=0} },
  /*
  ** .pp
  ** If \fIunset\fP, Mutt will remove your address (see the ``$alternates''
  ** command) from the list of recipients when replying to a message.
  */
  { "mh_purge",		DT_BOOL, R_NONE, {.l=OPTMHPURGE}, {.l=0} },
  /*
  ** .pp
  ** When \fIunset\fP, mutt will mimic mh's behavior and rename deleted messages
  ** to \fI,<old file name>\fP in mh folders instead of really deleting
  ** them. This leaves the message on disk but makes programs reading the folder
  ** ignore it. If the variable is \fIset\fP, the message files will simply be
  ** deleted.
  ** .pp
  ** This option is similar to $$maildir_trash for Maildir folders.
  */
  { "mh_seq_flagged",	DT_STR, R_NONE, {.p=&MhFlagged}, {.p="flagged"} },
  /*
  ** .pp
  ** The name of the MH sequence used for flagged messages.
  */
  { "mh_seq_replied",	DT_STR, R_NONE, {.p=&MhReplied}, {.p="replied"} },
  /*
  ** .pp
  ** The name of the MH sequence used to tag replied messages.
  */
  { "mh_seq_unseen",	DT_STR, R_NONE, {.p=&MhUnseen}, {.p="unseen"} },
  /*
  ** .pp
  ** The name of the MH sequence used for unseen messages.
  */
  { "mime_forward",	DT_QUAD, R_NONE, {.l=OPT_MIMEFWD}, {.l=MUTT_NO} },
  /*
  ** .pp
  ** When \fIset\fP, the message you are forwarding will be attached as a
  ** separate \fCmessage/rfc822\fP MIME part instead of included in the main body of the
  ** message.  This is useful for forwarding MIME messages so the receiver
  ** can properly view the message as it was delivered to you. If you like
  ** to switch between MIME and not MIME from mail to mail, set this
  ** variable to ``ask-no'' or ``ask-yes''.
  ** .pp
  ** Also see $$forward_decode and $$mime_forward_decode.
  */
  { "mime_forward_decode", DT_BOOL, R_NONE, {.l=OPTMIMEFORWDECODE}, {.l=0} },
  /*
  ** .pp
  ** Controls the decoding of complex MIME messages into \fCtext/plain\fP when
  ** forwarding a message while $$mime_forward is \fIset\fP. Otherwise
  ** $$forward_decode is used instead.
  */
  { "mime_fwd",		DT_SYN,  R_NONE, {.p="mime_forward"}, {.p=0} },
  /*
  */
  { "mime_forward_rest", DT_QUAD, R_NONE, {.l=OPT_MIMEFWDREST}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** When forwarding multiple attachments of a MIME message from the attachment
  ** menu, attachments which cannot be decoded in a reasonable manner will
  ** be attached to the newly composed message if this option is \fIset\fP.
  */
  { "mime_type_query_command", DT_STR, R_NONE, {.p=&MimeTypeQueryCmd}, {.p=0} },
  /*
  ** .pp
  ** This specifies a command to run, to determine the mime type of a
  ** new attachment when composing a message.  Unless
  ** $$mime_type_query_first is set, this will only be run if the
  ** attachment's extension is not found in the mime.types file.
  ** .pp
  ** The string may contain a ``%s'', which will be substituted with the
  ** attachment filename.  Mutt will add quotes around the string substituted
  ** for ``%s'' automatically according to shell quoting rules, so you should
  ** avoid adding your own.  If no ``%s'' is found in the string, Mutt will
  ** append the attachment filename to the end of the string.
  ** .pp
  ** The command should output a single line containing the
  ** attachment's mime type.
  ** .pp
  ** Suggested values are ``xdg-mime query filetype'' or
  ** ``file -bi''.
  */
  { "mime_type_query_first", DT_BOOL, R_NONE, {.l=OPTMIMETYPEQUERYFIRST}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, the $$mime_type_query_command will be run before the
  ** mime.types lookup.
  */
#ifdef MIXMASTER
  { "mix_entry_format", DT_STR,  R_NONE, {.p=&MixEntryFormat}, {.p="%4n %c %-16s %a"} },
  /*
  ** .pp
  ** This variable describes the format of a remailer line on the mixmaster
  ** chain selection screen.  The following \fCprintf(3)\fP-like sequences are
  ** supported:
  ** .dl
  ** .dt %n .dd The running number on the menu.
  ** .dt %c .dd Remailer capabilities.
  ** .dt %s .dd The remailer's short name.
  ** .dt %a .dd The remailer's e-mail address.
  ** .de
  ** .pp
  ** (Mixmaster only)
  */
  { "mixmaster",	DT_CMD_PATH, R_NONE, {.p=&Mixmaster}, {.p=MIXMASTER} },
  /*
  ** .pp
  ** This variable contains the path to the Mixmaster binary on your
  ** system.  It is used with various sets of parameters to gather the
  ** list of known remailers, and to finally send a message through the
  ** mixmaster chain. (Mixmaster only)
  */
#endif
  { "move",		DT_QUAD, R_NONE, {.l=OPT_MOVE}, {.l=MUTT_NO} },
  /*
  ** .pp
  ** Controls whether or not Mutt will move read messages
  ** from your spool mailbox to your $$mbox mailbox, or as a result of
  ** a ``$mbox-hook'' command.
  */
  { "muttlisp_inline_eval", DT_BOOL, R_NONE, {.l=OPTMUTTLISPINLINEEVAL}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will evaluate bare parenthesis arguments to commands
  ** as MuttLisp expressions.
  */
  { "narrow_tree",	DT_BOOL, R_TREE|R_INDEX, {.l=OPTNARROWTREE}, {.l=0} },
  /*
  ** .pp
  ** This variable, when \fIset\fP, makes the thread tree narrower, allowing
  ** deeper threads to fit on the screen.
  */
#ifdef USE_SOCKET
  { "net_inc",	DT_NUM,	 R_NONE, {.p=&NetInc}, {.l=10} },
  /*
  ** .pp
  ** Operations that expect to transfer a large amount of data over the
  ** network will update their progress every $$net_inc kilobytes.
  ** If set to 0, no progress messages will be displayed.
  ** .pp
  ** See also $$read_inc, $$write_inc and $$net_inc.
  */
#endif
  { "new_mail_command",	DT_CMD_PATH, R_NONE, {.p=&NewMailCmd}, {.p=0} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will call this command after a new message is received.
  ** See the $$status_format documentation for the values that can be formatted
  ** into this command.
  */
  { "pager",		DT_CMD_PATH, R_NONE, {.p=&Pager}, {.p="builtin"} },
  /*
  ** .pp
  ** This variable specifies which pager you would like to use to view
  ** messages. The value ``builtin'' means to use the built-in pager, otherwise this
  ** variable should specify the pathname of the external pager you would
  ** like to use.
  ** .pp
  ** The string may contain a ``%s'', which will be substituted with
  ** the generated message filename.  Mutt will add quotes around the
  ** string substituted for ``%s'' automatically according to shell
  ** quoting rules, so you should avoid adding your own.  If no ``%s''
  ** is found in the string, Mutt will append the message filename to
  ** the end of the string.
  ** .pp
  ** Using an external pager may have some disadvantages: Additional
  ** keystrokes are necessary because you can't call mutt functions
  ** directly from the pager, and screen resizes cause lines longer than
  ** the screen width to be badly formatted in the help menu.
  ** .pp
  ** When using an external pager, also see $$prompt_after which defaults
  ** \fIset\fP.
  */
  { "pager_context",	DT_NUM,	 R_NONE, {.p=&PagerContext}, {.l=0} },
  /*
  ** .pp
  ** This variable controls the number of lines of context that are given
  ** when displaying the next or previous page in the internal pager.  By
  ** default, Mutt will display the line after the last one on the screen
  ** at the top of the next page (0 lines of context).
  ** .pp
  ** This variable also specifies the amount of context given for search
  ** results. If positive, this many lines will be given before a match,
  ** if 0, the match will be top-aligned.
  */
  { "pager_format",	DT_STR,	 R_PAGER, {.p=&PagerFmt}, {.p="-%Z- %C/%m: %-20.20n   %s%*  -- (%P)"} },
  /*
  ** .pp
  ** This variable controls the format of the one-line message ``status''
  ** displayed before each message in either the internal or an external
  ** pager.  The valid sequences are listed in the $$index_format
  ** section.
  */
  { "pager_index_lines",DT_NUM,	 R_PAGER, {.p=&PagerIndexLines}, {.l=0} },
  /*
  ** .pp
  ** Determines the number of lines of a mini-index which is shown when in
  ** the pager.  The current message, unless near the top or bottom of the
  ** folder, will be roughly one third of the way down this mini-index,
  ** giving the reader the context of a few messages before and after the
  ** message.  This is useful, for example, to determine how many messages
  ** remain to be read in the current thread.  One of the lines is reserved
  ** for the status bar from the index, so a setting of 6
  ** will only show 5 lines of the actual index.  A value of 0 results in
  ** no index being shown.  If the number of messages in the current folder
  ** is less than $$pager_index_lines, then the index will only use as
  ** many lines as it needs.
  */
  { "pager_skip_quoted_context", DT_NUM, R_NONE, {.p=&PagerSkipQuotedContext}, {.l=0} },
  /*
  ** .pp
  ** Determines the number of lines of context to show before the
  ** unquoted text when using \fC$<skip-quoted>\fP. When set to a
  ** positive number at most that many lines of the previous quote are
  ** displayed. If the previous quote is shorter the whole quote is
  ** displayed.
  */
  { "pager_stop",	DT_BOOL, R_NONE, {.l=OPTPAGERSTOP}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, the internal-pager will \fBnot\fP move to the next message
  ** when you are at the end of a message and invoke the \fC<next-page>\fP
  ** function.
  */
  { "pattern_format", DT_STR, R_NONE, {.p=&PatternFormat}, {.p="%2n %-15e  %d"} },
  /*
  ** .pp
  ** This variable describes the format of the ``pattern completion'' menu. The
  ** following \fCprintf(3)\fP-style sequences are understood:
  ** .dl
  ** .dt %d  .dd pattern description
  ** .dt %e  .dd pattern expression
  ** .dt %n  .dd index number
  ** .de
  ** .pp
  */
  { "pgp_auto_decode", DT_BOOL, R_NONE, {.l=OPTPGPAUTODEC}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will automatically attempt to decrypt traditional PGP
  ** messages whenever the user performs an operation which ordinarily would
  ** result in the contents of the message being operated on.  For example,
  ** if the user displays a pgp-traditional message which has not been manually
  ** checked with the \fC$<check-traditional-pgp>\fP function, mutt will automatically
  ** check the message for traditional pgp.
  */
  { "pgp_create_traditional",	DT_SYN, R_NONE, {.p="pgp_autoinline"}, {.p=0} },
  { "pgp_autoinline",		DT_BOOL, R_NONE, {.l=OPTPGPAUTOINLINE}, {.l=0} },
  /*
  ** .pp
  ** This option controls whether Mutt generates old-style inline
  ** (traditional) PGP encrypted or signed messages under certain
  ** circumstances.  This can be overridden by use of the pgp menu,
  ** when inline is not required.  The GPGME backend does not support
  ** this option.
  ** .pp
  ** Note that Mutt might automatically use PGP/MIME for messages
  ** which consist of more than a single MIME part.  Mutt can be
  ** configured to ask before sending PGP/MIME messages when inline
  ** (traditional) would not work.
  ** .pp
  ** Also see the $$pgp_mime_auto variable.
  ** .pp
  ** Also note that using the old-style PGP message format is \fBstrongly\fP
  ** \fBdeprecated\fP.
  ** (PGP only)
  */
  { "pgp_check_exit",	DT_BOOL, R_NONE, {.l=OPTPGPCHECKEXIT}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will check the exit code of the PGP subprocess when
  ** signing or encrypting.  A non-zero exit code means that the
  ** subprocess failed.
  ** (PGP only)
  */
  { "pgp_check_gpg_decrypt_status_fd", DT_BOOL, R_NONE, {.l=OPTPGPCHECKGPGDECRYPTSTATUSFD}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will check the status file descriptor output
  ** of $$pgp_decrypt_command and $$pgp_decode_command for GnuPG status codes
  ** indicating successful decryption.  This will check for the presence of
  ** DECRYPTION_OKAY, absence of DECRYPTION_FAILED, and that all
  ** PLAINTEXT occurs between the BEGIN_DECRYPTION and END_DECRYPTION
  ** status codes.
  ** .pp
  ** If \fIunset\fP, mutt will instead match the status fd output
  ** against $$pgp_decryption_okay.
  ** (PGP only)
  */
  { "pgp_clearsign_command",	DT_STR,	R_NONE, {.p=&PgpClearSignCommand}, {.p=0} },
  /*
  ** .pp
  ** This format is used to create an old-style ``clearsigned'' PGP
  ** message.  Note that the use of this format is \fBstrongly\fP
  ** \fBdeprecated\fP.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pgp_decode_command",       DT_STR, R_NONE, {.p=&PgpDecodeCommand}, {.p=0} },
  /*
  ** .pp
  ** This format strings specifies a command which is used to decode
  ** application/pgp attachments.
  ** .pp
  ** The PGP command formats have their own set of \fCprintf(3)\fP-like sequences:
  ** .dl
  ** .dt %p .dd Expands to PGPPASSFD=0 when a pass phrase is needed, to an empty
  **            string otherwise. Note: This may be used with a %? construct.
  ** .dt %f .dd Expands to the name of a file containing a message.
  ** .dt %s .dd Expands to the name of a file containing the signature part
  ** .          of a \fCmultipart/signed\fP attachment when verifying it.
  ** .dt %a .dd The value of $$pgp_sign_as if set, otherwise the value
  **            of $$pgp_default_key.
  ** .dt %r .dd One or more key IDs (or fingerprints if available).
  ** .de
  ** .pp
  ** For examples on how to configure these formats for the various versions
  ** of PGP which are floating around, see the pgp and gpg sample configuration files in
  ** the \fCsamples/\fP subdirectory which has been installed on your system
  ** alongside the documentation.
  ** (PGP only)
  */
  { "pgp_decrypt_command", 	DT_STR, R_NONE, {.p=&PgpDecryptCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to decrypt a PGP encrypted message.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pgp_decryption_okay",	DT_RX,  R_NONE, {.p=&PgpDecryptionOkay}, {.p=0} },
  /*
  ** .pp
  ** If you assign text to this variable, then an encrypted PGP
  ** message is only considered successfully decrypted if the output
  ** from $$pgp_decrypt_command contains the text.  This is used to
  ** protect against a spoofed encrypted message, with multipart/encrypted
  ** headers but containing a block that is not actually encrypted.
  ** (e.g. simply signed and ascii armored text).
  ** .pp
  ** Note that if $$pgp_check_gpg_decrypt_status_fd is set, this variable
  ** is ignored.
  ** (PGP only)
  */
  { "pgp_self_encrypt_as",	DT_SYN,  R_NONE, {.p="pgp_default_key"}, {.p=0} },
  { "pgp_default_key",		DT_STR,	 R_NONE, {.p=&PgpDefaultKey}, {.p=0} },
  /*
  ** .pp
  ** This is the default key-pair to use for PGP operations.  It will be
  ** used for encryption (see $$postpone_encrypt and $$pgp_self_encrypt).
  ** .pp
  ** It will also be used for signing unless $$pgp_sign_as is set.
  ** .pp
  ** The (now deprecated) \fIpgp_self_encrypt_as\fP is an alias for this
  ** variable, and should no longer be used.
  ** (PGP only)
  */
  { "pgp_encrypt_only_command", DT_STR, R_NONE, {.p=&PgpEncryptOnlyCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to encrypt a body part without signing it.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pgp_encrypt_sign_command",	DT_STR, R_NONE, {.p=&PgpEncryptSignCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to both sign and encrypt a body part.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pgp_entry_format", DT_STR,  R_NONE, {.p=&PgpEntryFormat}, {.p="%4n %t%f %4l/0x%k %-4a %2c %u"} },
  /*
  ** .pp
  ** This variable allows you to customize the PGP key selection menu to
  ** your personal taste. This string is similar to $$index_format, but
  ** has its own set of \fCprintf(3)\fP-like sequences:
  ** .dl
  ** .dt %n     .dd number
  ** .dt %k     .dd key id
  ** .dt %u     .dd user id
  ** .dt %a     .dd algorithm
  ** .dt %l     .dd key length
  ** .dt %f     .dd flags
  ** .dt %c     .dd capabilities
  ** .dt %t     .dd trust/validity of the key-uid association
  ** .dt %[<s>] .dd date of the key where <s> is an \fCstrftime(3)\fP expression
  ** .de
  ** .pp
  ** (PGP only)
  */
  { "pgp_export_command", 	DT_STR, R_NONE, {.p=&PgpExportCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to export a public key from the user's
  ** key ring.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pgp_getkeys_command",	DT_STR, R_NONE, {.p=&PgpGetkeysCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is invoked whenever Mutt needs to fetch the public key associated with
  ** an email address.  Of the sequences supported by $$pgp_decode_command, %r is
  ** the only \fCprintf(3)\fP-like sequence used with this format.  Note that
  ** in this case, %r expands to the email address, not the public key ID (the key ID is
  ** unknown, which is why Mutt is invoking this command).
  ** (PGP only)
  */
  { "pgp_good_sign",	DT_RX,  R_NONE, {.p=&PgpGoodSign}, {.p=0} },
  /*
  ** .pp
  ** If you assign a text to this variable, then a PGP signature is only
  ** considered verified if the output from $$pgp_verify_command contains
  ** the text. Use this variable if the exit code from the command is 0
  ** even for bad signatures.
  ** (PGP only)
  */
  { "pgp_ignore_subkeys", DT_BOOL, R_NONE, {.l=OPTPGPIGNORESUB}, {.l=1} },
  /*
  ** .pp
  ** Setting this variable will cause Mutt to ignore OpenPGP subkeys. Instead,
  ** the principal key will inherit the subkeys' capabilities.  \fIUnset\fP this
  ** if you want to play interesting key selection games.
  ** (PGP only)
  */
  { "pgp_import_command",	DT_STR, R_NONE, {.p=&PgpImportCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to import a key from a message into
  ** the user's public key ring.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pgp_list_pubring_command", DT_STR, R_NONE, {.p=&PgpListPubringCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to list the public key ring's contents.  The
  ** output format must be analogous to the one used by
  ** .ts
  ** gpg --list-keys --with-colons --with-fingerprint
  ** .te
  ** .pp
  ** This format is also generated by the \fCmutt_pgpring\fP utility which comes
  ** with mutt.
  ** .pp
  ** Note: gpg's \fCfixed-list-mode\fP option should not be used.  It
  ** produces a different date format which may result in mutt showing
  ** incorrect key generation dates.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** Note that in this case, %r expands to the search string, which is a list of
  ** one or more quoted values such as email address, name, or keyid.
  ** (PGP only)
  */
  { "pgp_list_secring_command",	DT_STR, R_NONE, {.p=&PgpListSecringCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to list the secret key ring's contents.  The
  ** output format must be analogous to the one used by:
  ** .ts
  ** gpg --list-keys --with-colons --with-fingerprint
  ** .te
  ** .pp
  ** This format is also generated by the \fCmutt_pgpring\fP utility which comes
  ** with mutt.
  ** .pp
  ** Note: gpg's \fCfixed-list-mode\fP option should not be used.  It
  ** produces a different date format which may result in mutt showing
  ** incorrect key generation dates.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** Note that in this case, %r expands to the search string, which is a list of
  ** one or more quoted values such as email address, name, or keyid.
  ** (PGP only)
  */
  { "pgp_long_ids",	DT_BOOL, R_NONE, {.l=OPTPGPLONGIDS}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, use 64 bit PGP key IDs, if \fIunset\fP use the normal 32 bit key IDs.
  ** NOTE: Internally, Mutt has transitioned to using fingerprints (or long key IDs
  ** as a fallback).  This option now only controls the display of key IDs
  ** in the key selection menu and a few other places.
  ** (PGP only)
  */
  { "pgp_mime_auto", DT_QUAD, R_NONE, {.l=OPT_PGPMIMEAUTO}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** This option controls whether Mutt will prompt you for
  ** automatically sending a (signed/encrypted) message using
  ** PGP/MIME when inline (traditional) fails (for any reason).
  ** .pp
  ** Also note that using the old-style PGP message format is \fBstrongly\fP
  ** \fBdeprecated\fP.
  ** (PGP only)
  */
  { "pgp_auto_traditional",	DT_SYN, R_NONE, {.p="pgp_replyinline"}, {.p=0} },
  { "pgp_replyinline",		DT_BOOL, R_NONE, {.l=OPTPGPREPLYINLINE}, {.l=0} },
  /*
  ** .pp
  ** Setting this variable will cause Mutt to always attempt to
  ** create an inline (traditional) message when replying to a
  ** message which is PGP encrypted/signed inline.  This can be
  ** overridden by use of the pgp menu, when inline is not
  ** required.  This option does not automatically detect if the
  ** (replied-to) message is inline; instead it relies on Mutt
  ** internals for previously checked/flagged messages.
  ** .pp
  ** Note that Mutt might automatically use PGP/MIME for messages
  ** which consist of more than a single MIME part.  Mutt can be
  ** configured to ask before sending PGP/MIME messages when inline
  ** (traditional) would not work.
  ** .pp
  ** Also see the $$pgp_mime_auto variable.
  ** .pp
  ** Also note that using the old-style PGP message format is \fBstrongly\fP
  ** \fBdeprecated\fP.
  ** (PGP only)
  **
  */
  { "pgp_retainable_sigs", DT_BOOL, R_NONE, {.l=OPTPGPRETAINABLESIG}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, signed and encrypted messages will consist of nested
  ** \fCmultipart/signed\fP and \fCmultipart/encrypted\fP body parts.
  ** .pp
  ** This is useful for applications like encrypted and signed mailing
  ** lists, where the outer layer (\fCmultipart/encrypted\fP) can be easily
  ** removed, while the inner \fCmultipart/signed\fP part is retained.
  ** (PGP only)
  */
  { "pgp_self_encrypt",    DT_BOOL, R_NONE, {.l=OPTPGPSELFENCRYPT}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, PGP encrypted messages will also be encrypted
  ** using the key in $$pgp_default_key.
  ** (PGP only)
  */
  { "pgp_show_unusable", DT_BOOL, R_NONE, {.l=OPTPGPSHOWUNUSABLE}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will display non-usable keys on the PGP key selection
  ** menu.  This includes keys which have been revoked, have expired, or
  ** have been marked as ``disabled'' by the user.
  ** (PGP only)
  */
  { "pgp_sign_as",	DT_STR,	 R_NONE, {.p=&PgpSignAs}, {.p=0} },
  /*
  ** .pp
  ** If you have a different key pair to use for signing, you should
  ** set this to the signing key.  Most people will only need to set
  ** $$pgp_default_key.  It is recommended that you use the keyid form
  ** to specify your key (e.g. \fC0x00112233\fP).
  ** (PGP only)
  */
  { "pgp_sign_command",		DT_STR, R_NONE, {.p=&PgpSignCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to create the detached PGP signature for a
  ** \fCmultipart/signed\fP PGP/MIME body part.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pgp_sort_keys",	DT_SORT|DT_SORT_KEYS, R_NONE, {.p=&PgpSortKeys}, {.l=SORT_ADDRESS} },
  /*
  ** .pp
  ** Specifies how the entries in the pgp menu are sorted. The
  ** following are legal values:
  ** .dl
  ** .dt address .dd sort alphabetically by user id
  ** .dt keyid   .dd sort alphabetically by key id
  ** .dt date    .dd sort by key creation date
  ** .dt trust   .dd sort by the trust of the key
  ** .de
  ** .pp
  ** If you prefer reverse order of the above values, prefix it with
  ** ``reverse-''.
  ** (PGP only)
  */
  { "pgp_strict_enc",	DT_BOOL, R_NONE, {.l=OPTPGPSTRICTENC}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will automatically encode PGP/MIME signed messages as
  ** quoted-printable.  Please note that unsetting this variable may
  ** lead to problems with non-verifyable PGP signatures, so only change
  ** this if you know what you are doing.
  ** (PGP only)
  */
  { "pgp_timeout",	DT_LNUM,	 R_NONE, {.p=&PgpTimeout}, {.l=300} },
  /*
  ** .pp
  ** The number of seconds after which a cached passphrase will expire if
  ** not used.
  ** (PGP only)
  */
  { "pgp_use_gpg_agent", DT_BOOL, R_NONE, {.l=OPTUSEGPGAGENT}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, mutt expects a \fCgpg-agent(1)\fP process will handle
  ** private key passphrase prompts.  If \fIunset\fP, mutt will prompt
  ** for the passphrase and pass it via stdin to the pgp command.
  ** .pp
  ** Note that as of version 2.1, GnuPG automatically spawns an agent
  ** and requires the agent be used for passphrase management.  Since
  ** that version is increasingly prevalent, this variable now
  ** defaults \fIset\fP.
  ** .pp
  ** Mutt works with a GUI or curses pinentry program.  A TTY pinentry
  ** should not be used.
  ** .pp
  ** If you are using an older version of GnuPG without an agent running,
  ** or another encryption program without an agent, you will need to
  ** \fIunset\fP this variable.
  ** (PGP only)
  */
  { "pgp_verify_command", 	DT_STR, R_NONE, {.p=&PgpVerifyCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to verify PGP signatures.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pgp_verify_key_command",	DT_STR, R_NONE, {.p=&PgpVerifyKeyCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to verify key information from the key selection
  ** menu.
  ** .pp
  ** This is a format string, see the $$pgp_decode_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (PGP only)
  */
  { "pipe_decode",	DT_BOOL, R_NONE, {.l=OPTPIPEDECODE}, {.l=0} },
  /*
  ** .pp
  ** Used in connection with the \fC<pipe-message>\fP function.  When \fIunset\fP,
  ** Mutt will pipe the messages without any preprocessing. When \fIset\fP, Mutt
  ** will attempt to decode the messages first.
  ** .pp
  ** Also see $$pipe_decode_weed, which controls whether headers will
  ** be weeded when this is \fIset\fP.
  */
  { "pipe_decode_weed",	DT_BOOL, R_NONE, {.l=OPTPIPEDECODEWEED}, {.l=1} },
  /*
  ** .pp
  ** For \fC<pipe-message>\fP, when $$pipe_decode is set, this further
  ** controls whether Mutt will weed headers.
  */
  { "pipe_sep",		DT_STR,	 R_NONE, {.p=&PipeSep}, {.p="\n"} },
  /*
  ** .pp
  ** The separator to add between messages when piping a list of tagged
  ** messages to an external Unix command.
  */
  { "pipe_split",	DT_BOOL, R_NONE, {.l=OPTPIPESPLIT}, {.l=0} },
  /*
  ** .pp
  ** Used in connection with the \fC<pipe-message>\fP function following
  ** \fC<tag-prefix>\fP.  If this variable is \fIunset\fP, when piping a list of
  ** tagged messages Mutt will concatenate the messages and will pipe them
  ** all concatenated.  When \fIset\fP, Mutt will pipe the messages one by one.
  ** In both cases the messages are piped in the current sorted order,
  ** and the $$pipe_sep separator is added after each message.
  */
#ifdef USE_POP
  { "pop_auth_try_all",	DT_BOOL, R_NONE, {.l=OPTPOPAUTHTRYALL}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will try all available authentication methods.
  ** When \fIunset\fP, Mutt will only fall back to other authentication
  ** methods if the previous methods are unavailable. If a method is
  ** available but authentication fails, Mutt will not connect to the POP server.
  */
  { "pop_authenticators", DT_STR, R_NONE, {.p=&PopAuthenticators}, {.p=0} },
  /*
  ** .pp
  ** This is a colon-delimited list of authentication methods mutt may
  ** attempt to use to log in to an POP server, in the order mutt should
  ** try them.  Authentication methods are either ``user'', ``apop'' or any
  ** SASL mechanism, e.g. ``digest-md5'', ``gssapi'' or ``cram-md5''.
  ** This option is case-insensitive. If this option is \fIunset\fP
  ** (the default) mutt will try all available methods, in order from
  ** most-secure to least-secure.
  ** .pp
  ** Example:
  ** .ts
  ** set pop_authenticators="digest-md5:apop:user"
  ** .te
  */
  { "pop_checkinterval", DT_NUM, R_NONE, {.p=&PopCheckTimeout}, {.l=60} },
  /*
  ** .pp
  ** This variable configures how often (in seconds) mutt should look for
  ** new mail in the currently selected mailbox if it is a POP mailbox.
  */
  { "pop_delete",	DT_QUAD, R_NONE, {.l=OPT_POPDELETE}, {.l=MUTT_ASKNO} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will delete successfully downloaded messages from the POP
  ** server when using the \fC$<fetch-mail>\fP function.  When \fIunset\fP, Mutt will
  ** download messages but also leave them on the POP server.
  */
  { "pop_host",		DT_STR,	 R_NONE, {.p=&PopHost}, {.p=0} },
  /*
  ** .pp
  ** The name of your POP server for the \fC$<fetch-mail>\fP function.  You
  ** can also specify an alternative port, username and password, i.e.:
  ** .ts
  ** [pop[s]://][username[:password]@]popserver[:port]
  ** .te
  ** .pp
  ** where ``[...]'' denotes an optional part.
  */
  { "pop_last",		DT_BOOL, R_NONE, {.l=OPTPOPLAST}, {.l=0} },
  /*
  ** .pp
  ** If this variable is \fIset\fP, mutt will try to use the ``\fCLAST\fP'' POP command
  ** for retrieving only unread messages from the POP server when using
  ** the \fC$<fetch-mail>\fP function.
  */
  { "pop_oauth_refresh_command", DT_STR, R_NONE, {.p=&PopOauthRefreshCmd}, {.p=0} },
  /*
  ** .pp
  ** The command to run to generate an OAUTH refresh token for
  ** authorizing your connection to your POP server.  This command will be
  ** run on every connection attempt that uses the OAUTHBEARER authentication
  ** mechanism.  See ``$oauth'' for details.
  */
  { "pop_pass",		DT_STR,	 R_NONE, {.p=&PopPass}, {.p=0} },
  /*
  ** .pp
  ** Specifies the password for your POP account.  If \fIunset\fP, Mutt will
  ** prompt you for your password when you open a POP mailbox.
  ** .pp
  ** \fBWarning\fP: you should only use this option when you are on a
  ** fairly secure machine, because the superuser can read your muttrc
  ** even if you are the only one who can read the file.
  */
  { "pop_reconnect",	DT_QUAD, R_NONE, {.l=OPT_POPRECONNECT}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** Controls whether or not Mutt will try to reconnect to the POP server if
  ** the connection is lost.
  */
  { "pop_user",		DT_STR,	 R_NONE, {.p=&PopUser}, {.p=0} },
  /*
  ** .pp
  ** Your login name on the POP server.
  ** .pp
  ** This variable defaults to your user name on the local machine.
  */
#endif /* USE_POP */
  { "post_indent_string",DT_STR, R_NONE, {.p=&PostIndentString}, {.p=0} },
  /*
  ** .pp
  ** Similar to the $$attribution variable, Mutt will append this
  ** string after the inclusion of a message which is being replied to.
  ** For a full listing of defined \fCprintf(3)\fP-like sequences see
  ** the section on $$index_format.
  */
  { "post_indent_str",  DT_SYN,  R_NONE, {.p="post_indent_string"}, {.p=0} },
  /*
  */
  { "postpone",		DT_QUAD, R_NONE, {.l=OPT_POSTPONE}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** Controls whether or not messages are saved in the $$postponed
  ** mailbox when you elect not to send immediately.
  ** .pp
  ** Also see the $$recall variable.
  */
  { "postponed",	DT_PATH, R_INDEX, {.p=&Postponed}, {.p="~/postponed"} },
  /*
  ** .pp
  ** Mutt allows you to indefinitely ``$postpone sending a message'' which
  ** you are editing.  When you choose to postpone a message, Mutt saves it
  ** in the mailbox specified by this variable.
  ** .pp
  ** Also see the $$postpone variable.
  */
  { "postpone_encrypt",    DT_BOOL, R_NONE, {.l=OPTPOSTPONEENCRYPT}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, postponed messages that are marked for encryption will be
  ** self-encrypted.  Mutt will first try to encrypt using the value specified
  ** in $$pgp_default_key or $$smime_default_key.  If those are not
  ** set, it will try the deprecated $$postpone_encrypt_as.
  ** (Crypto only)
  */
  { "postpone_encrypt_as", DT_STR,  R_NONE, {.p=&PostponeEncryptAs}, {.p=0} },
  /*
  ** .pp
  ** This is a deprecated fall-back variable for $$postpone_encrypt.
  ** Please use $$pgp_default_key or $$smime_default_key.
  ** (Crypto only)
  */
#ifdef USE_SOCKET
  { "preconnect",	DT_STR, R_NONE, {.p=&Preconnect}, {.p=0} },
  /*
  ** .pp
  ** If \fIset\fP, a shell command to be executed if mutt fails to establish
  ** a connection to the server. This is useful for setting up secure
  ** connections, e.g. with \fCssh(1)\fP. If the command returns a  nonzero
  ** status, mutt gives up opening the server. Example:
  ** .ts
  ** set preconnect="ssh -f -q -L 1234:mailhost.net:143 mailhost.net \(rs
  ** sleep 20 < /dev/null > /dev/null"
  ** .te
  ** .pp
  ** Mailbox ``foo'' on ``mailhost.net'' can now be reached
  ** as ``{localhost:1234}foo''.
  ** .pp
  ** Note: For this example to work, you must be able to log in to the
  ** remote machine without having to enter a password.
  */
#endif /* USE_SOCKET */
  { "print",		DT_QUAD, R_NONE, {.l=OPT_PRINT}, {.l=MUTT_ASKNO} },
  /*
  ** .pp
  ** Controls whether or not Mutt really prints messages.
  ** This is set to ``ask-no'' by default, because some people
  ** accidentally hit ``p'' often.
  */
  { "print_command",	DT_CMD_PATH, R_NONE, {.p=&PrintCmd}, {.p="lpr"} },
  /*
  ** .pp
  ** This specifies the command pipe that should be used to print messages.
  */
  { "print_cmd",	DT_SYN,  R_NONE, {.p="print_command"}, {.p=0} },
  /*
  */
  { "print_decode",	DT_BOOL, R_NONE, {.l=OPTPRINTDECODE}, {.l=1} },
  /*
  ** .pp
  ** Used in connection with the \fC<print-message>\fP function.  If this
  ** option is \fIset\fP, the message is decoded before it is passed to the
  ** external command specified by $$print_command.  If this option
  ** is \fIunset\fP, no processing will be applied to the message when
  ** printing it.  The latter setting may be useful if you are using
  ** some advanced printer filter which is able to properly format
  ** e-mail messages for printing.
  ** .pp
  ** Also see $$print_decode_weed, which controls whether headers will
  ** be weeded when this is \fIset\fP.
  */
  { "print_decode_weed", DT_BOOL, R_NONE, {.l=OPTPRINTDECODEWEED}, {.l=1} },
  /*
  ** .pp
  ** For \fC<print-message>\fP, when $$print_decode is set, this
  ** further controls whether Mutt will weed headers.
  */
  { "print_split",	DT_BOOL, R_NONE, {.l=OPTPRINTSPLIT},  {.l=0} },
  /*
  ** .pp
  ** Used in connection with the \fC<print-message>\fP function.  If this option
  ** is \fIset\fP, the command specified by $$print_command is executed once for
  ** each message which is to be printed.  If this option is \fIunset\fP,
  ** the command specified by $$print_command is executed only once, and
  ** all the messages are concatenated, with a form feed as the message
  ** separator.
  ** .pp
  ** Those who use the \fCenscript\fP(1) program's mail-printing mode will
  ** most likely want to \fIset\fP this option.
  */
  { "prompt_after",	DT_BOOL, R_NONE, {.l=OPTPROMPTAFTER}, {.l=1} },
  /*
  ** .pp
  ** If you use an \fIexternal\fP $$pager, setting this variable will
  ** cause Mutt to prompt you for a command when the pager exits rather
  ** than returning to the index menu.  If \fIunset\fP, Mutt will return to the
  ** index menu when the external pager exits.
  */
  { "query_command",	DT_CMD_PATH, R_NONE, {.p=&QueryCmd}, {.p=0} },
  /*
  ** .pp
  ** This specifies the command Mutt will use to make external address
  ** queries.  The string may contain a ``%s'', which will be substituted
  ** with the query string the user types.  Mutt will add quotes around the
  ** string substituted for ``%s'' automatically according to shell quoting
  ** rules, so you should avoid adding your own.  If no ``%s'' is found in
  ** the string, Mutt will append the user's query to the end of the string.
  ** See ``$query'' for more information.
  */
  { "query_format",	DT_STR, R_NONE, {.p=&QueryFormat}, {.p="%4c %t %-25.25a %-25.25n %?e?(%e)?"} },
  /*
  ** .pp
  ** This variable describes the format of the ``query'' menu. The
  ** following \fCprintf(3)\fP-style sequences are understood:
  ** .dl
  ** .dt %a  .dd destination address
  ** .dt %c  .dd current entry number
  ** .dt %e  .dd extra information *
  ** .dt %n  .dd destination name
  ** .dt %t  .dd ``*'' if current entry is tagged, a space otherwise
  ** .dt %>X .dd right justify the rest of the string and pad with ``X''
  ** .dt %|X .dd pad to the end of the line with ``X''
  ** .dt %*X .dd soft-fill with character ``X'' as pad
  ** .de
  ** .pp
  ** For an explanation of ``soft-fill'', see the $$index_format documentation.
  ** .pp
  ** * = can be optionally printed if nonzero, see the $$status_format documentation.
  */
  { "quit",		DT_QUAD, R_NONE, {.l=OPT_QUIT}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** This variable controls whether ``quit'' and ``exit'' actually quit
  ** from mutt.  If this option is \fIset\fP, they do quit, if it is \fIunset\fP, they
  ** have no effect, and if it is set to \fIask-yes\fP or \fIask-no\fP, you are
  ** prompted for confirmation when you try to quit.
  */
  { "quote_regexp",	DT_RX,	 R_PAGER, {.p=&QuoteRegexp}, {.p="^([ \t]*[|>:}#])+"} },
  /*
  ** .pp
  ** A regular expression used in the internal pager to determine quoted
  ** sections of text in the body of a message. Quoted text may be filtered
  ** out using the \fC<toggle-quoted>\fP command, or colored according to the
  ** ``color quoted'' family of directives.
  ** .pp
  ** Higher levels of quoting may be colored differently (``color quoted1'',
  ** ``color quoted2'', etc.). The quoting level is determined by removing
  ** the last character from the matched text and recursively reapplying
  ** the regular expression until it fails to produce a match.
  ** .pp
  ** Match detection may be overridden by the $$smileys regular expression.
  */
  { "read_inc",		DT_NUM,	 R_NONE, {.p=&ReadInc}, {.l=10} },
  /*
  ** .pp
  ** If set to a value greater than 0, Mutt will display which message it
  ** is currently on when reading a mailbox or when performing search actions
  ** such as search and limit. The message is printed after
  ** this many messages have been read or searched (e.g., if set to 25, Mutt will
  ** print a message when it is at message 25, and then again when it gets
  ** to message 50).  This variable is meant to indicate progress when
  ** reading or searching large mailboxes which may take some time.
  ** When set to 0, only a single message will appear before the reading
  ** the mailbox.
  ** .pp
  ** Also see the $$write_inc, $$net_inc and $$time_inc variables and the
  ** ``$tuning'' section of the manual for performance considerations.
  */
  { "read_only",	DT_BOOL, R_NONE, {.l=OPTREADONLY}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, all folders are opened in read-only mode.
  */
  { "realname",		DT_STR,	 R_BOTH, {.p=&Realname}, {.p=0} },
  /*
  ** .pp
  ** This variable specifies what ``real'' or ``personal'' name should be used
  ** when sending messages.
  ** .pp
  ** By default, this is the GECOS field from \fC/etc/passwd\fP.  Note that this
  ** variable will \fInot\fP be used when the user has set a real name
  ** in the $$from variable.
  */
  { "recall",		DT_QUAD, R_NONE, {.l=OPT_RECALL}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** Controls whether or not Mutt recalls postponed messages
  ** when composing a new message.
  ** .pp
  ** Setting this variable to \fIyes\fP is not generally useful, and thus not
  ** recommended.  Note that the \fC<recall-message>\fP function can be used
  ** to manually recall postponed messages.
  ** .pp
  ** Also see $$postponed variable.
  */
  { "record",		DT_PATH, R_NONE, {.p=&Outbox}, {.p="~/sent"} },
  /*
  ** .pp
  ** This specifies the file into which your outgoing messages should be
  ** appended.  (This is meant as the primary method for saving a copy of
  ** your messages, but another way to do this is using the ``$my_hdr''
  ** command to create a ``Bcc:'' field with your email address in it.)
  ** .pp
  ** The value of \fI$$record\fP is overridden by the $$force_name and
  ** $$save_name variables, and the ``$fcc-hook'' command.  Also see $$copy
  ** and $$write_bcc.
  ** .pp
  ** Multiple mailboxes may be specified if $$fcc_delimiter is
  ** set to a string delimiter.
  */
  { "reflow_space_quotes",	DT_BOOL, R_NONE, {.l=OPTREFLOWSPACEQUOTES}, {.l=1} },
  /*
  ** .pp
  ** This option controls how quotes from format=flowed messages are displayed
  ** in the pager and when replying (with $$text_flowed \fIunset\fP).
  ** When set, this option adds spaces after each level of quote marks, turning
  ** ">>>foo" into "> > > foo".
  ** .pp
  ** \fBNote:\fP If $$reflow_text is \fIunset\fP, this option has no effect.
  ** Also, this option does not affect replies when $$text_flowed is \fIset\fP.
  */
  { "reflow_text",	DT_BOOL, R_NONE, {.l=OPTREFLOWTEXT}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will reformat paragraphs in text/plain
  ** parts marked format=flowed.  If \fIunset\fP, Mutt will display paragraphs
  ** unaltered from how they appear in the message body.  See RFC3676 for
  ** details on the \fIformat=flowed\fP format.
  ** .pp
  ** Also see $$reflow_wrap, and $$wrap.
  */
  { "reflow_wrap",	DT_NUM,	R_NONE, {.p=&ReflowWrap}, {.l=78} },
  /*
  ** .pp
  ** This variable controls the maximum paragraph width when reformatting text/plain
  ** parts when $$reflow_text is \fIset\fP.  When the value is 0, paragraphs will
  ** be wrapped at the terminal's right margin.  A positive value sets the
  ** paragraph width relative to the left margin.  A negative value set the
  ** paragraph width relative to the right margin.
  ** .pp
  ** Also see $$wrap.
  */
  /* L10N:
     $reply_regexp default value.

     This is a regular expression that matches reply subject lines.
     By default, it only matches an initial "Re: ", which is the
     standardized Latin prefix.

     However, many locales have other prefixes that are commonly used
     too, such as Aw in Germany.  To add other prefixes, modify the first
     parenthesized expression, such as:
        "^(re|aw)
     you can add multiple values, for example:
        "^(re|aw|se)

     Important:
     - Use all lower case letters.
     - Don't remove the 're' prefix from the list of choices.
     - Please test the value you use inside Mutt.  A mistake here
       will break Mutt's threading behavior.  Note: the header cache
       can interfere with testing, so be sure to test with $header_cache
       unset.
  */
  { "reply_regexp",	DT_RX|DT_L10N_STR, R_INDEX|R_RESORT|R_RESORT_INIT, {.p=&ReplyRegexp}, {.p=N_("^(re)(\\[[0-9]+\\])*:[ \t]*")} },
  /*
  ** .pp
  ** A regular expression used to recognize reply messages when
  ** threading and replying. The default value corresponds to the
  ** standard Latin "Re:" prefix.
  ** .pp
  ** This value may have been localized by the translator for your
  ** locale, adding other prefixes that are common in the locale. You
  ** can add your own prefixes by appending inside \fC"^(re)"\fP.  For
  ** example: \fC"^(re|se)"\fP or \fC"^(re|aw|se)"\fP.
  ** .pp
  ** The second parenthesized expression matches zero or more
  ** bracketed numbers following the prefix, such as \fC"Re[1]: "\fP.
  ** The initial \fC"\\["\fP means a literal left-bracket character.
  ** Note the backslash must be doubled when used inside a double
  ** quoted string in the muttrc.  \fC"[0-9]+"\fP means one or more
  ** numbers.  \fC"\\]"\fP means a literal right-bracket.  Finally the
  ** whole parenthesized expression has a \fC"*"\fP suffix, meaning it
  ** can occur zero or more times.
  ** .pp
  ** The last part matches a colon followed by an optional space or
  ** tab.  Note \fC"\t"\fP is converted to a literal tab inside a
  ** double quoted string.  If you use a single quoted string, you
  ** would have to type an actual tab character, and would need to
  ** convert the double-backslashes to single backslashes.
  ** .pp
  ** Note: the result of this regexp match against the subject is
  ** stored in the header cache.  Mutt isn't smart enough to
  ** invalidate a header cache entry based on changing $$reply_regexp,
  ** so if you aren't seeing correct values in the index, try
  ** temporarily turning off the header cache.  If that fixes the
  ** problem, then once the variable is set to your liking, remove
  ** your stale header cache files and turn the header cache back on.
  */
  { "reply_self",	DT_BOOL, R_NONE, {.l=OPTREPLYSELF}, {.l=0} },
  /*
  ** .pp
  ** If \fIunset\fP and you are replying to a message sent by you, Mutt will
  ** assume that you want to reply to the recipients of that message rather
  ** than to yourself.
  ** .pp
  ** Also see the ``$alternates'' command.
  */
  { "reply_to",		DT_QUAD, R_NONE, {.l=OPT_REPLYTO}, {.l=MUTT_ASKYES} },
  /*
  ** .pp
  ** If \fIset\fP, when replying to a message, Mutt will use the address listed
  ** in the Reply-to: header as the recipient of the reply.  If \fIunset\fP,
  ** it will use the address in the From: header field instead.  This
  ** option is useful for reading a mailing list that sets the Reply-To:
  ** header field to the list address and you want to send a private
  ** message to the author of a message.
  */
  { "resolve",		DT_BOOL, R_NONE, {.l=OPTRESOLVE}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, the cursor will be automatically advanced to the next
  ** (possibly undeleted) message whenever a command that modifies the
  ** current message is executed.
  */
  { "resume_draft_files", DT_BOOL, R_NONE, {.l=OPTRESUMEDRAFTFILES}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, draft files (specified by \fC-H\fP on the command
  ** line) are processed similarly to when resuming a postponed
  ** message.  Recipients are not prompted for; send-hooks are not
  ** evaluated; no alias expansion takes place; user-defined headers
  ** and signatures are not added to the message.
  */
  { "resume_edited_draft_files", DT_BOOL, R_NONE, {.l=OPTRESUMEEDITEDDRAFTFILES}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, draft files previously edited (via \fC-E -H\fP on
  ** the command line) will have $$resume_draft_files automatically
  ** set when they are used as a draft file again.
  ** .pp
  ** The first time a draft file is saved, mutt will add a header,
  ** X-Mutt-Resume-Draft to the saved file.  The next time the draft
  ** file is read in, if mutt sees the header, it will set
  ** $$resume_draft_files.
  ** .pp
  ** This option is designed to prevent multiple signatures,
  ** user-defined headers, and other processing effects from being
  ** made multiple times to the draft file.
  */
  { "reverse_alias",	DT_BOOL, R_BOTH, {.l=OPTREVALIAS}, {.l=0} },
  /*
  ** .pp
  ** This variable controls whether or not Mutt will display the ``personal''
  ** name from your aliases in the index menu if it finds an alias that
  ** matches the message's sender.  For example, if you have the following
  ** alias:
  ** .ts
  ** alias juser abd30425@somewhere.net (Joe User)
  ** .te
  ** .pp
  ** and then you receive mail which contains the following header:
  ** .ts
  ** From: abd30425@somewhere.net
  ** .te
  ** .pp
  ** It would be displayed in the index menu as ``Joe User'' instead of
  ** ``abd30425@somewhere.net.''  This is useful when the person's e-mail
  ** address is not human friendly.
  */
  { "reverse_name",	DT_BOOL, R_BOTH, {.l=OPTREVNAME}, {.l=0} },
  /*
  ** .pp
  ** It may sometimes arrive that you receive mail to a certain machine,
  ** move the messages to another machine, and reply to some the messages
  ** from there.  If this variable is \fIset\fP, the default \fIFrom:\fP line of
  ** the reply messages is built using the address where you received the
  ** messages you are replying to \fBif\fP that address matches your
  ** ``$alternates''.  If the variable is \fIunset\fP, or the address that would be
  ** used doesn't match your ``$alternates'', the \fIFrom:\fP line will use
  ** your address on the current machine.
  ** .pp
  ** Also see the ``$alternates'' command and $$reverse_realname.
  */
  { "reverse_realname",	DT_BOOL, R_BOTH, {.l=OPTREVREAL}, {.l=1} },
  /*
  ** .pp
  ** This variable fine-tunes the behavior of the $$reverse_name feature.
  ** .pp
  ** When it is \fIunset\fP, Mutt will remove the real name part of a
  ** matching address.  This allows the use of the email address
  ** without having to also use what the sender put in the real name
  ** field.
  ** .pp
  ** When it is \fIset\fP, Mutt will use the matching address as-is.
  ** .pp
  ** In either case, a missing real name will be filled in afterwards
  ** using the value of $$realname.
  */
  { "rfc2047_parameters", DT_BOOL, R_NONE, {.l=OPTRFC2047PARAMS}, {.l=1} },
  /*
  ** .pp
  ** When this variable is \fIset\fP, Mutt will decode RFC2047-encoded MIME
  ** parameters. You want to set this variable when mutt suggests you
  ** to save attachments to files named like:
  ** .ts
  ** =?iso-8859-1?Q?file=5F=E4=5F991116=2Ezip?=
  ** .te
  ** .pp
  ** When this variable is \fIset\fP interactively, the change won't be
  ** active until you change folders.
  ** .pp
  ** Note that this use of RFC2047's encoding is explicitly
  ** prohibited by the standard, but nevertheless encountered in the
  ** wild.
  ** .pp
  ** Also note that setting this parameter will \fInot\fP have the effect
  ** that mutt \fIgenerates\fP this kind of encoding.  Instead, mutt will
  ** unconditionally use the encoding specified in RFC2231.
  */
  { "save_address",	DT_BOOL, R_NONE, {.l=OPTSAVEADDRESS}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, mutt will take the sender's full address when choosing a
  ** default folder for saving a mail. If $$save_name or $$force_name
  ** is \fIset\fP too, the selection of the Fcc folder will be changed as well.
  */
  { "save_empty",	DT_BOOL, R_NONE, {.l=OPTSAVEEMPTY}, {.l=1} },
  /*
  ** .pp
  ** When \fIunset\fP, mailboxes which contain no saved messages will be removed
  ** when closed (the exception is $$spoolfile which is never removed).
  ** If \fIset\fP, mailboxes are never removed.
  ** .pp
  ** \fBNote:\fP This only applies to mbox and MMDF folders, Mutt does not
  ** delete MH and Maildir directories.
  */
  { "save_history",     DT_NUM,  R_NONE, {.p=&SaveHist}, {.l=0} },
  /*
  ** .pp
  ** This variable controls the size of the history (per category) saved in the
  ** $$history_file file.
  */
  { "save_name",	DT_BOOL, R_NONE, {.l=OPTSAVENAME}, {.l=0} },
  /*
  ** .pp
  ** This variable controls how copies of outgoing messages are saved.
  ** When \fIset\fP, a check is made to see if a mailbox specified by the
  ** recipient address exists (this is done by searching for a mailbox in
  ** the $$folder directory with the \fIusername\fP part of the
  ** recipient address).  If the mailbox exists, the outgoing message will
  ** be saved to that mailbox, otherwise the message is saved to the
  ** $$record mailbox.
  ** .pp
  ** Also see the $$force_name variable.
  */
  { "score", 		DT_BOOL, R_NONE, {.l=OPTSCORE}, {.l=1} },
  /*
  ** .pp
  ** When this variable is \fIunset\fP, scoring is turned off.  This can
  ** be useful to selectively disable scoring for certain folders when the
  ** $$score_threshold_delete variable and related are used.
  **
  */
  { "score_threshold_delete", DT_NUM, R_NONE, {.p=&ScoreThresholdDelete}, {.l=-1} },
  /*
  ** .pp
  ** Messages which have been assigned a score equal to or lower than the value
  ** of this variable are automatically marked for deletion by mutt.  Since
  ** mutt scores are always greater than or equal to zero, the default setting
  ** of this variable will never mark a message for deletion.
  */
  { "score_threshold_flag", DT_NUM, R_NONE, {.p=&ScoreThresholdFlag}, {.l=9999} },
  /*
  ** .pp
  ** Messages which have been assigned a score greater than or equal to this
  ** variable's value are automatically marked "flagged".
  */
  { "score_threshold_read", DT_NUM, R_NONE, {.p=&ScoreThresholdRead}, {.l=-1} },
  /*
  ** .pp
  ** Messages which have been assigned a score equal to or lower than the value
  ** of this variable are automatically marked as read by mutt.  Since
  ** mutt scores are always greater than or equal to zero, the default setting
  ** of this variable will never mark a message read.
  */
  { "search_context",	DT_NUM,  R_NONE, {.p=&SearchContext}, {.l=0} },
  /*
  ** .pp
  ** For the pager, this variable specifies the number of lines shown
  ** before search results. By default, search results will be top-aligned.
  */
  { "send_charset",	DT_STR,  R_NONE, {.p=&SendCharset}, {.p="us-ascii:iso-8859-1:utf-8"} },
  /*
  ** .pp
  ** A colon-delimited list of character sets for outgoing messages. Mutt will use the
  ** first character set into which the text can be converted exactly.
  ** If your $$charset is not ``iso-8859-1'' and recipients may not
  ** understand ``UTF-8'', it is advisable to include in the list an
  ** appropriate widely used standard character set (such as
  ** ``iso-8859-2'', ``koi8-r'' or ``iso-2022-jp'') either instead of or after
  ** ``iso-8859-1''.
  ** .pp
  ** In case the text cannot be converted into one of these exactly,
  ** mutt uses $$charset as a fallback.
  */
  { "send_multipart_alternative", DT_QUAD, R_NONE, {.l=OPT_SENDMULTIPARTALT}, {.l=MUTT_NO} },
  /*
  ** .pp
  ** If \fIset\fP, Mutt will generate a multipart/alternative
  ** container and an alternative part using the filter script specified in
  ** $$send_multipart_alternative_filter.
  ** See the section ``MIME Multipart/Alternative'' ($alternative-order).
  ** .pp
  ** Note that enabling multipart/alternative is not compatible with inline
  ** PGP encryption.  Mutt will prompt to use PGP/MIME in that case.
  */
  { "send_multipart_alternative_filter", DT_CMD_PATH, R_NONE, {.p=&SendMultipartAltFilter}, {.p=0} },
  /*
  ** .pp
  ** This specifies a filter script, which will convert the main
  ** (composed) message of the email to an alternative format.  The
  ** message will be piped to the filter's stdin.  The expected output
  ** of the filter is the generated mime type, e.g. text/html,
  ** followed by a blank line, and then the converted content.
  ** See the section ``MIME Multipart/Alternative'' ($alternative-order).
  */
  { "sendmail",	DT_CMD_PATH, R_NONE, {.p=&Sendmail}, {.p=SENDMAIL " -oem -oi"} },
  /*
  ** .pp
  ** Specifies the program and arguments used to deliver mail sent by Mutt.
  ** Mutt expects that the specified program interprets additional
  ** arguments as recipient addresses.  Mutt appends all recipients after
  ** adding a \fC--\fP delimiter (if not already present).  Additional
  ** flags, such as for $$use_8bitmime, $$use_envelope_from,
  ** $$dsn_notify, or $$dsn_return will be added before the delimiter.
  ** .pp
  ** \fBNote:\fP This command is invoked differently from most other
  ** commands in Mutt.  It is tokenized by space, and invoked directly
  ** via \fCexecvp(3)\fP with an array of arguments - so commands or
  ** arguments with spaces in them are not supported.  The shell is
  ** not used to run the command, so shell quoting is also not
  ** supported.
  ** .pp
  ** \fBSee also:\fP $$write_bcc.
  */
  { "sendmail_wait",	DT_NUM,  R_NONE, {.p=&SendmailWait}, {.l=0} },
  /*
  ** .pp
  ** Specifies the number of seconds to wait for the $$sendmail process
  ** to finish before giving up and putting delivery in the background.
  ** .pp
  ** Mutt interprets the value of this variable as follows:
  ** .dl
  ** .dt >0 .dd number of seconds to wait for sendmail to finish before continuing
  ** .dt 0  .dd wait forever for sendmail to finish
  ** .dt <0 .dd always put sendmail in the background without waiting
  ** .de
  ** .pp
  ** Note that if you specify a value other than 0, the output of the child
  ** process will be put in a temporary file.  If there is some error, you
  ** will be informed as to where to find the output.
  */
  { "shell",		DT_CMD_PATH, R_NONE, {.p=&Shell}, {.p=0} },
  /*
  ** .pp
  ** Command to use when spawning a subshell.  By default, the user's login
  ** shell from \fC/etc/passwd\fP is used.
  */
#ifdef USE_SIDEBAR
  { "sidebar_delim_chars", DT_STR, R_SIDEBAR, {.p=&SidebarDelimChars}, {.p="/."} },
  /*
  ** .pp
  ** This contains the list of characters which you would like to treat
  ** as folder separators for displaying paths in the sidebar.
  ** .pp
  ** Local mail is often arranged in directories: `dir1/dir2/mailbox'.
  ** .ts
  ** set sidebar_delim_chars='/'
  ** .te
  ** .pp
  ** IMAP mailboxes are often named: `folder1.folder2.mailbox'.
  ** .ts
  ** set sidebar_delim_chars='.'
  ** .te
  ** .pp
  ** \fBSee also:\fP $$sidebar_short_path, $$sidebar_folder_indent, $$sidebar_indent_string.
  */
  { "sidebar_divider_char", DT_STR, R_SIDEBAR, {.p=&SidebarDividerChar}, {.p="|"} },
  /*
  ** .pp
  ** This specifies the characters to be drawn between the sidebar (when
  ** visible) and the other Mutt panels. ASCII and Unicode line-drawing
  ** characters are supported.
  */
  { "sidebar_folder_indent", DT_BOOL, R_SIDEBAR, {.l=OPTSIDEBARFOLDERINDENT}, {.l=0} },
  /*
  ** .pp
  ** Set this to indent mailboxes in the sidebar.
  ** .pp
  ** \fBSee also:\fP $$sidebar_short_path, $$sidebar_indent_string, $$sidebar_delim_chars.
  */
  { "sidebar_format", DT_STR, R_SIDEBAR, {.p=&SidebarFormat}, {.p="%B%*  %n"} },
  /*
  ** .pp
  ** This variable allows you to customize the sidebar display. This string is
  ** similar to $$index_format, but has its own set of \fCprintf(3)\fP-like
  ** sequences:
  ** .dl
  ** .dt %B  .dd Name of the mailbox
  ** .dt %S  .dd * Size of mailbox (total number of messages)
  ** .dt %N  .dd * Number of unread messages in the mailbox
  ** .dt %n  .dd N if mailbox has new mail, blank otherwise
  ** .dt %F  .dd * Number of Flagged messages in the mailbox
  ** .dt %!  .dd ``!'' : one flagged message;
  **             ``!!'' : two flagged messages;
  **             ``n!'' : n flagged messages (for n > 2).
  **             Otherwise prints nothing.
  ** .dt %d  .dd * @ Number of deleted messages
  ** .dt %L  .dd * @ Number of messages after limiting
  ** .dt %t  .dd * @ Number of tagged messages
  ** .dt %>X .dd right justify the rest of the string and pad with ``X''
  ** .dt %|X .dd pad to the end of the line with ``X''
  ** .dt %*X .dd soft-fill with character ``X'' as pad
  ** .de
  ** .pp
  ** * = Can be optionally printed if nonzero
  ** @ = Only applicable to the current folder
  ** .pp
  ** In order to use %S, %N, %F, and %!, $$mail_check_stats must
  ** be \fIset\fP.  When thus set, a suggested value for this option is
  ** "%B%?F? [%F]?%* %?N?%N/?%S".
  */
  { "sidebar_indent_string", DT_STR, R_SIDEBAR, {.p=&SidebarIndentString}, {.p="  "} },
  /*
  ** .pp
  ** This specifies the string that is used to indent mailboxes in the sidebar.
  ** It defaults to two spaces.
  ** .pp
  ** \fBSee also:\fP $$sidebar_short_path, $$sidebar_folder_indent, $$sidebar_delim_chars.
  */
  { "sidebar_new_mail_only", DT_BOOL, R_SIDEBAR, {.l=OPTSIDEBARNEWMAILONLY}, {.l=0} },
  /*
  ** .pp
  ** When set, the sidebar will only display mailboxes containing new, or
  ** flagged, mail.
  ** .pp
  ** \fBSee also:\fP $sidebar_whitelist.
  */
  { "sidebar_next_new_wrap", DT_BOOL, R_NONE, {.l=OPTSIDEBARNEXTNEWWRAP}, {.l=0} },
  /*
  ** .pp
  ** When set, the \fC<sidebar-next-new>\fP command will not stop and the end of
  ** the list of mailboxes, but wrap around to the beginning. The
  ** \fC<sidebar-prev-new>\fP command is similarly affected, wrapping around to
  ** the end of the list.
  */
  { "sidebar_relative_shortpath_indent", DT_BOOL, R_SIDEBAR, {.l=OPTSIDEBARRELSPINDENT}, {.l=0} },
  /*
  ** .pp
  ** When set, this option changes how $$sidebar_short_path and
  ** $$sidebar_folder_indent perform shortening and indentation: both
  ** will look at the previous sidebar entries and shorten/indent
  ** relative to the most recent parent.
  ** .pp
  ** An example of this option set/unset for mailboxes listed in this
  ** order, with $$sidebar_short_path=yes,
  ** $$sidebar_folder_indent=yes, and $$sidebar_indent_string="→":
  ** .dl
  ** .dt \fBmailbox\fP  .dd \fBset\fP   .dd \fBunset\fP
  ** .dt \fC=a.b\fP     .dd \fC=a.b\fP  .dd \fC→b\fP
  ** .dt \fC=a.b.c.d\fP .dd \fC→c.d\fP  .dd \fC→→→d\fP
  ** .dt \fC=a.b.e\fP   .dd \fC→e\fP    .dd \fC→→e\fP
  ** .de
  ** .pp
  ** The second line illustrates most clearly.  With this option set,
  ** \fC=a.b.c.d\fP is shortened relative to \fC=a.b\fP, becoming
  ** \fCc.d\fP; it is also indented one place relative to \fC=a.b\fP.
  ** With this option unset \fC=a.b.c.d\fP is always shortened to the
  ** last part of the mailbox, \fCd\fP and is indented three places,
  ** with respect to $$folder (represented by '=').
  ** .pp
  ** When set, the third line will also be indented and shortened
  ** relative to the first line.
  */
  { "sidebar_short_path", DT_BOOL, R_SIDEBAR, {.l=OPTSIDEBARSHORTPATH}, {.l=0} },
  /*
  ** .pp
  ** By default the sidebar will show the mailbox's path, relative to the
  ** $$folder variable. Setting \fCsidebar_shortpath=yes\fP will shorten the
  ** names relative to the previous name. Here's an example:
  ** .dl
  ** .dt \fBshortpath=no\fP .dd \fBshortpath=yes\fP .dd \fBshortpath=yes, folderindent=yes, indentstr=".."\fP
  ** .dt \fCfruit\fP        .dd \fCfruit\fP         .dd \fCfruit\fP
  ** .dt \fCfruit.apple\fP  .dd \fCapple\fP         .dd \fC..apple\fP
  ** .dt \fCfruit.banana\fP .dd \fCbanana\fP        .dd \fC..banana\fP
  ** .dt \fCfruit.cherry\fP .dd \fCcherry\fP        .dd \fC..cherry\fP
  ** .de
  ** .pp
  ** \fBSee also:\fP $$sidebar_delim_chars, $$sidebar_folder_indent, $$sidebar_indent_string.
  */
  { "sidebar_sort_method", DT_SORT|DT_SORT_SIDEBAR, R_SIDEBAR, {.p=&SidebarSortMethod}, {.l=SORT_ORDER} },
  /*
  ** .pp
  ** Specifies how to sort mailbox entries in the sidebar.  By default, the
  ** entries are sorted alphabetically.  Valid values:
  ** .il
  ** .dd alpha (alphabetically)
  ** .dd count (all message count)
  ** .dd flagged (flagged message count)
  ** .dd name (alphabetically)
  ** .dd new (unread message count)
  ** .dd path (alphabetically)
  ** .dd unread (unread message count)
  ** .dd unsorted
  ** .ie
  ** .pp
  ** You may optionally use the ``reverse-'' prefix to specify reverse sorting
  ** order (example: ``\fCset sidebar_sort_method=reverse-alpha\fP'').
  */
  { "sidebar_use_mailbox_shortcuts", DT_BOOL, R_SIDEBAR, {.l=OPTSIDEBARUSEMBSHORTCUTS}, {.l=0} },
  /*
  ** .pp
  ** When set, sidebar mailboxes will be displayed with mailbox shortcut prefixes
  ** "=" or "~".
  ** .pp
  ** When unset, the sidebar will trim off a matching $$folder prefix
  ** but otherwise not use mailbox shortcuts.
  */
  { "sidebar_visible", DT_BOOL, R_REFLOW, {.l=OPTSIDEBAR}, {.l=0} },
  /*
  ** .pp
  ** This specifies whether or not to show sidebar. The sidebar shows a list of
  ** all your mailboxes.
  ** .pp
  ** \fBSee also:\fP $$sidebar_format, $$sidebar_width
  */
  { "sidebar_width", DT_NUM, R_REFLOW, {.p=&SidebarWidth}, {.l=30} },
  /*
  ** .pp
  ** This controls the width of the sidebar.  It is measured in screen columns.
  ** For example: sidebar_width=20 could display 20 ASCII characters, or 10
  ** Chinese characters.
  */
#endif
  { "sig_dashes",	DT_BOOL, R_NONE, {.l=OPTSIGDASHES}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, a line containing ``-- '' (note the trailing space) will be inserted before your
  ** $$signature.  It is \fBstrongly\fP recommended that you not \fIunset\fP
  ** this variable unless your signature contains just your name.  The
  ** reason for this is because many software packages use ``-- \n'' to
  ** detect your signature.  For example, Mutt has the ability to highlight
  ** the signature in a different color in the built-in pager.
  */
  { "sig_on_top",	DT_BOOL, R_NONE, {.l=OPTSIGONTOP}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, the signature will be included before any quoted or forwarded
  ** text.  It is \fBstrongly\fP recommended that you do not set this variable
  ** unless you really know what you are doing, and are prepared to take
  ** some heat from netiquette guardians.
  */
  { "signature",	DT_PATH, R_NONE, {.p=&Signature}, {.p="~/.signature"} },
  /*
  ** .pp
  ** Specifies the filename of your signature, which is appended to all
  ** outgoing messages.   If the filename ends with a pipe (``|''), it is
  ** assumed that filename is a shell command and input should be read from
  ** its standard output.
  */
  { "simple_search",	DT_STR,	 R_NONE, {.p=&SimpleSearch}, {.p="~f %s | ~s %s"} },
  /*
  ** .pp
  ** Specifies how Mutt should expand a simple search into a real search
  ** pattern.  A simple search is one that does not contain any of the ``~'' pattern
  ** modifiers.  See ``$patterns'' for more information on search patterns.
  ** .pp
  ** For example, if you simply type ``joe'' at a search or limit prompt, Mutt
  ** will automatically expand it to the value specified by this variable by
  ** replacing ``%s'' with the supplied string.
  ** For the default value, ``joe'' would be expanded to: ``~f joe | ~s joe''.
  */
  { "size_show_bytes",	DT_BOOL, R_MENU, {.l=OPTSIZESHOWBYTES}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, message sizes will display bytes for values less than
  ** 1 kilobyte.  See $formatstrings-size.
  */
  { "size_show_fractions", DT_BOOL, R_MENU, {.l=OPTSIZESHOWFRACTIONS}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, message sizes will be displayed with a single decimal value
  ** for sizes from 0 to 10 kilobytes and 1 to 10 megabytes.
  ** See $formatstrings-size.
  */
  { "size_show_mb",	DT_BOOL, R_MENU, {.l=OPTSIZESHOWMB}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP, message sizes will display megabytes for values greater than
  ** or equal to 1 megabyte.  See $formatstrings-size.
  */
  { "size_units_on_left", DT_BOOL, R_MENU, {.l=OPTSIZEUNITSONLEFT}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, message sizes units will be displayed to the left of the number.
  ** See $formatstrings-size.
  */
  { "sleep_time",	DT_NUM, R_NONE, {.p=&SleepTime}, {.l=1} },
  /*
  ** .pp
  ** Specifies time, in seconds, to pause while displaying certain informational
  ** messages, while moving from folder to folder and after expunging
  ** messages from the current folder.  The default is to pause one second, so
  ** a value of zero for this option suppresses the pause.
  */
  { "smart_wrap",	DT_BOOL, R_PAGER_FLOW, {.l=OPTWRAP}, {.l=1} },
  /*
  ** .pp
  ** Controls the display of lines longer than the screen width in the
  ** internal pager. If \fIset\fP, long lines are wrapped at a word boundary.  If
  ** \fIunset\fP, lines are simply wrapped at the screen edge. Also see the
  ** $$markers variable.
  */
  { "smileys",		DT_RX,	 R_PAGER, {.p=&Smileys}, {.p="(>From )|(:[-^]?[][)(><}{|/DP])"} },
  /*
  ** .pp
  ** The \fIpager\fP uses this variable to catch some common false
  ** positives of $$quote_regexp, most notably smileys and not consider
  ** a line quoted text if it also matches $$smileys. This mostly
  ** happens at the beginning of a line.
  */



  { "smime_ask_cert_label",	DT_BOOL, R_NONE, {.l=OPTASKCERTLABEL}, {.l=1} },
  /*
  ** .pp
  ** This flag controls whether you want to be asked to enter a label
  ** for a certificate about to be added to the database or not. It is
  ** \fIset\fP by default.
  ** (S/MIME only)
  */
  { "smime_ca_location",	DT_PATH, R_NONE, {.p=&SmimeCALocation}, {.p=0} },
  /*
  ** .pp
  ** This variable contains the name of either a directory, or a file which
  ** contains trusted certificates for use with OpenSSL.
  ** (S/MIME only)
  */
  { "smime_certificates",	DT_PATH, R_NONE, {.p=&SmimeCertificates}, {.p=0} },
  /*
  ** .pp
  ** Since for S/MIME there is no pubring/secring as with PGP, mutt has to handle
  ** storage and retrieval of keys by itself. This is very basic right
  ** now, and keys and certificates are stored in two different
  ** directories, both named as the hash-value retrieved from
  ** OpenSSL. There is an index file which contains mailbox-address
  ** keyid pairs, and which can be manually edited. This option points to
  ** the location of the certificates.
  ** (S/MIME only)
  */
  { "smime_decrypt_command", 	DT_STR, R_NONE, {.p=&SmimeDecryptCommand}, {.p=0} },
  /*
  ** .pp
  ** This format string specifies a command which is used to decrypt
  ** \fCapplication/x-pkcs7-mime\fP attachments.
  ** .pp
  ** The OpenSSL command formats have their own set of \fCprintf(3)\fP-like sequences
  ** similar to PGP's:
  ** .dl
  ** .dt %f .dd Expands to the name of a file containing a message.
  ** .dt %s .dd Expands to the name of a file containing the signature part
  ** .          of a \fCmultipart/signed\fP attachment when verifying it.
  ** .dt %k .dd The key-pair specified with $$smime_default_key
  ** .dt %c .dd One or more certificate IDs.
  ** .dt %a .dd The algorithm used for encryption.
  ** .dt %d .dd The message digest algorithm specified with $$smime_sign_digest_alg.
  ** .dt %C .dd CA location:  Depending on whether $$smime_ca_location
  ** .          points to a directory or file, this expands to
  ** .          ``-CApath $$smime_ca_location'' or ``-CAfile $$smime_ca_location''.
  ** .de
  ** .pp
  ** For examples on how to configure these formats, see the \fCsmime.rc\fP in
  ** the \fCsamples/\fP subdirectory which has been installed on your system
  ** alongside the documentation.
  ** (S/MIME only)
  */
  { "smime_decrypt_use_default_key",	DT_BOOL, R_NONE, {.l=OPTSDEFAULTDECRYPTKEY}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP (default) this tells mutt to use the default key for decryption. Otherwise,
  ** if managing multiple certificate-key-pairs, mutt will try to use the mailbox-address
  ** to determine the key to use. It will ask you to supply a key, if it can't find one.
  ** (S/MIME only)
  */
  { "smime_self_encrypt_as",	DT_SYN,  R_NONE, {.p="smime_default_key"}, {.p=0} },
  { "smime_default_key",		DT_STR,	 R_NONE, {.p=&SmimeDefaultKey}, {.p=0} },
  /*
  ** .pp
  ** This is the default key-pair to use for S/MIME operations, and must be
  ** set to the keyid (the hash-value that OpenSSL generates) to work properly.
  ** .pp
  ** It will be used for encryption (see $$postpone_encrypt and
  ** $$smime_self_encrypt). If GPGME is enabled, this is the key id displayed
  ** by gpgsm.
  ** .pp
  ** It will be used for decryption unless $$smime_decrypt_use_default_key
  ** is \fIunset\fP.
  ** .pp
  ** It will also be used for signing unless $$smime_sign_as is set.
  ** .pp
  ** The (now deprecated) \fIsmime_self_encrypt_as\fP is an alias for this
  ** variable, and should no longer be used.
  ** (S/MIME only)
  */
  { "smime_encrypt_command", 	DT_STR, R_NONE, {.p=&SmimeEncryptCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to create encrypted S/MIME messages.
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
  { "smime_encrypt_with",	DT_STR,	 R_NONE, {.p=&SmimeCryptAlg}, {.p="aes256"} },
  /*
  ** .pp
  ** This sets the algorithm that should be used for encryption.
  ** Valid choices are ``aes128'', ``aes192'', ``aes256'', ``des'', ``des3'', ``rc2-40'', ``rc2-64'', ``rc2-128''.
  ** (S/MIME only)
  */
  { "smime_get_cert_command", 	DT_STR, R_NONE, {.p=&SmimeGetCertCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to extract X509 certificates from a PKCS7 structure.
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
  { "smime_get_cert_email_command", 	DT_STR, R_NONE, {.p=&SmimeGetCertEmailCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to extract the mail address(es) used for storing
  ** X509 certificates, and for verification purposes (to check whether the
  ** certificate was issued for the sender's mailbox).
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
  { "smime_get_signer_cert_command", 	DT_STR, R_NONE, {.p=&SmimeGetSignerCertCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to extract only the signers X509 certificate from a S/MIME
  ** signature, so that the certificate's owner may get compared to the
  ** email's ``From:'' field.
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
  { "smime_import_cert_command", 	DT_STR, R_NONE, {.p=&SmimeImportCertCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to import a certificate via smime_keys.
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
  { "smime_is_default", DT_BOOL,  R_NONE, {.l=OPTSMIMEISDEFAULT}, {.l=0} },
  /*
  ** .pp
  ** The default behavior of mutt is to use PGP on all auto-sign/encryption
  ** operations. To override and to use OpenSSL instead this must be \fIset\fP.
  ** However, this has no effect while replying, since mutt will automatically
  ** select the same application that was used to sign/encrypt the original
  ** message.  (Note that this variable can be overridden by unsetting $$crypt_autosmime.)
  ** (S/MIME only)
  */
  { "smime_keys",		DT_PATH, R_NONE, {.p=&SmimeKeys}, {.p=0} },
  /*
  ** .pp
  ** Since for S/MIME there is no pubring/secring as with PGP, mutt has to handle
  ** storage and retrieval of keys/certs by itself. This is very basic right now,
  ** and stores keys and certificates in two different directories, both
  ** named as the hash-value retrieved from OpenSSL. There is an index file
  ** which contains mailbox-address keyid pair, and which can be manually
  ** edited. This option points to the location of the private keys.
  ** (S/MIME only)
  */
  { "smime_pk7out_command", 	DT_STR, R_NONE, {.p=&SmimePk7outCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to extract PKCS7 structures of S/MIME signatures,
  ** in order to extract the public X509 certificate(s).
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
  { "smime_self_encrypt",    DT_BOOL, R_NONE, {.l=OPTSMIMESELFENCRYPT}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, S/MIME encrypted messages will also be encrypted
  ** using the certificate in $$smime_default_key.
  ** (S/MIME only)
  */
  { "smime_sign_as",	DT_STR,	 R_NONE, {.p=&SmimeSignAs}, {.p=0} },
  /*
  ** .pp
  ** If you have a separate key to use for signing, you should set this
  ** to the signing key. Most people will only need to set $$smime_default_key.
  ** (S/MIME only)
  */
  { "smime_sign_command", 	DT_STR, R_NONE, {.p=&SmimeSignCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to created S/MIME signatures of type
  ** \fCmultipart/signed\fP, which can be read by all mail clients.
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.  NOTE: %c and %k will default
  ** to $$smime_sign_as if set, otherwise $$smime_default_key.
  ** (S/MIME only)
  */
  { "smime_sign_digest_alg",	DT_STR,	 R_NONE, {.p=&SmimeDigestAlg}, {.p="sha256"} },
  /*
  ** .pp
  ** This sets the algorithm that should be used for the signature message digest.
  ** Valid choices are ``md5'', ``sha1'', ``sha224'', ``sha256'', ``sha384'', ``sha512''.
  ** (S/MIME only)
  */
  { "smime_sign_opaque_command", 	DT_STR, R_NONE, {.p=&SmimeSignOpaqueCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to created S/MIME signatures of type
  ** \fCapplication/x-pkcs7-signature\fP, which can only be handled by mail
  ** clients supporting the S/MIME extension.
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
  { "smime_timeout",		DT_LNUM,	 R_NONE, {.p=&SmimeTimeout}, {.l=300} },
  /*
  ** .pp
  ** The number of seconds after which a cached passphrase will expire if
  ** not used.
  ** (S/MIME only)
  */
  { "smime_verify_command", 	DT_STR, R_NONE, {.p=&SmimeVerifyCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to verify S/MIME signatures of type \fCmultipart/signed\fP.
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
  { "smime_verify_opaque_command", 	DT_STR, R_NONE, {.p=&SmimeVerifyOpaqueCommand}, {.p=0} },
  /*
  ** .pp
  ** This command is used to verify S/MIME signatures of type
  ** \fCapplication/x-pkcs7-mime\fP.
  ** .pp
  ** This is a format string, see the $$smime_decrypt_command command for
  ** possible \fCprintf(3)\fP-like sequences.
  ** (S/MIME only)
  */
#ifdef USE_SMTP
  { "smtp_authenticators", DT_STR, R_NONE, {.p=&SmtpAuthenticators}, {.p=0} },
  /*
  ** .pp
  ** This is a colon-delimited list of authentication methods mutt may
  ** attempt to use to log in to an SMTP server, in the order mutt should
  ** try them.  Authentication methods are any SASL mechanism, e.g.
  ** ``digest-md5'', ``gssapi'' or ``cram-md5''.
  ** This option is case-insensitive. If it is ``unset''
  ** (the default) mutt will try all available methods, in order from
  ** most-secure to least-secure.
  ** .pp
  ** Example:
  ** .ts
  ** set smtp_authenticators="digest-md5:cram-md5"
  ** .te
  */
  { "smtp_oauth_refresh_command", DT_STR, R_NONE, {.p=&SmtpOauthRefreshCmd}, {.p=0} },
  /*
  ** .pp
  ** The command to run to generate an OAUTH refresh token for
  ** authorizing your connection to your SMTP server.  This command will be
  ** run on every connection attempt that uses the OAUTHBEARER authentication
  ** mechanism.  See ``$oauth'' for details.
  */
  { "smtp_pass", 	DT_STR,  R_NONE, {.p=&SmtpPass}, {.p=0} },
  /*
  ** .pp
  ** Specifies the password for your SMTP account.  If \fIunset\fP, Mutt will
  ** prompt you for your password when you first send mail via SMTP.
  ** See $$smtp_url to configure mutt to send mail via SMTP.
  ** .pp
  ** \fBWarning\fP: you should only use this option when you are on a
  ** fairly secure machine, because the superuser can read your muttrc even
  ** if you are the only one who can read the file.
  */
  { "smtp_url",		DT_STR, R_NONE, {.p=&SmtpUrl}, {.p=0} },
  /*
  ** .pp
  ** Defines the SMTP smarthost where sent messages should relayed for
  ** delivery. This should take the form of an SMTP URL, e.g.:
  ** .ts
  ** smtp[s]://[user[:pass]@]host[:port]
  ** .te
  ** .pp
  ** where ``[...]'' denotes an optional part.
  ** Setting this variable overrides the value of the $$sendmail
  ** variable.
  ** .pp
  ** Also see $$write_bcc.
  */
#endif /* USE_SMTP */
  { "sort",		DT_SORT, R_INDEX|R_RESORT, {.p=&Sort}, {.l=SORT_DATE} },
  /*
  ** .pp
  ** Specifies how to sort messages in the ``index'' menu.  Valid values
  ** are:
  ** .il
  ** .dd date or date-sent
  ** .dd date-received
  ** .dd from
  ** .dd mailbox-order (unsorted)
  ** .dd score
  ** .dd size
  ** .dd spam
  ** .dd subject
  ** .dd threads
  ** .dd to
  ** .ie
  ** .pp
  ** You may optionally use the ``reverse-'' prefix to specify reverse sorting
  ** order (example: ``\fCset sort=reverse-date-sent\fP'').
  ** .pp
  ** For values except ``threads'', this provides the primary sort
  ** method.  When two message sort values are equal, $$sort_aux will
  ** be used for a secondary sort.
  ** .pp
  ** When set to ``threads'', Mutt threads messages in the index. It
  ** uses the variable $$sort_thread_groups to sort between threads
  ** (at the top/root level), and $$sort_aux to sort sub-threads and
  ** children.
  */
  { "sort_alias",	DT_SORT|DT_SORT_ALIAS,	R_NONE,	{.p=&SortAlias}, {.l=SORT_ALIAS} },
  /*
  ** .pp
  ** Specifies how the entries in the ``alias'' menu are sorted.  The
  ** following are legal values:
  ** .il
  ** .dd address (sort alphabetically by email address)
  ** .dd alias (sort alphabetically by alias name)
  ** .dd unsorted (leave in order specified in .muttrc)
  ** .ie
  */
  { "sort_aux",		DT_SORT|DT_SORT_AUX, R_INDEX|R_RESORT_BOTH, {.p=&SortAux}, {.l=SORT_DATE} },
  /*
  ** .pp
  ** For non-threaded mode, this provides a secondary sort for
  ** messages in the ``index'' menu, used when the $$sort value is
  ** equal for two messages.
  ** .pp
  ** When sorting by threads, this variable controls how the branches
  ** of the thread trees are sorted.  This can be set to any value
  ** that $$sort can, except ``threads'' (in that case, mutt will just
  ** use ``date-sent'').  You can also specify the ``last-'' prefix in
  ** addition to the ``reverse-'' prefix, but ``last-'' must come
  ** after ``reverse-''.  The ``last-'' prefix causes messages to be
  ** sorted against its siblings by which has the last descendant,
  ** using the rest of $$sort_aux as an ordering.  For instance,
  ** .ts
  ** set sort_aux=last-date-received
  ** .te
  ** .pp
  ** would mean that if a new message is received in a sub-thread,
  ** that sub-thread becomes the last one displayed.
  ** .pp
  ** Note: For reversed-threads $$sort
  ** order, $$sort_aux is reversed again (which is not the right thing to do,
  ** but kept to not break any existing configuration setting).
  */
  { "sort_browser",	DT_SORT|DT_SORT_BROWSER, R_NONE, {.p=&BrowserSort}, {.l=SORT_SUBJECT} },
  /*
  ** .pp
  ** Specifies how to sort entries in the file browser.  By default, the
  ** entries are sorted alphabetically.  Valid values:
  ** .il
  ** .dd alpha (alphabetically)
  ** .dd count
  ** .dd date
  ** .dd size
  ** .dd unread
  ** .dd unsorted
  ** .ie
  ** .pp
  ** You may optionally use the ``reverse-'' prefix to specify reverse sorting
  ** order (example: ``\fCset sort_browser=reverse-date\fP'').
  */
  { "sort_browser_mailboxes", DT_SORT|DT_SORT_BROWSER, R_NONE, {.p=&BrowserSortMailboxes}, {.l=SORT_ORDER} },
  /*
  ** .pp
  ** Specifies how to sort entries in the mailbox browser.  By default, the
  ** entries are unsorted, displayed in the same order as listed
  ** in the ``mailboxes'' command.  Valid values:
  ** .il
  ** .dd alpha (alphabetically)
  ** .dd count
  ** .dd date
  ** .dd size
  ** .dd unread
  ** .dd unsorted
  ** .ie
  ** .pp
  ** You may optionally use the ``reverse-'' prefix to specify reverse sorting
  ** order (example: ``\fCset sort_browser_mailboxes=reverse-alpha\fP'').
  */
  { "sort_re",		DT_BOOL, R_INDEX|R_RESORT|R_RESORT_INIT, {.l=OPTSORTRE}, {.l=1} },
  /*
  ** .pp
  ** This variable is only useful when sorting by threads with
  ** $$strict_threads \fIunset\fP.  In that case, it changes the heuristic
  ** mutt uses to thread messages by subject.  With $$sort_re \fIset\fP, mutt will
  ** only attach a message as the child of another message by subject if
  ** the subject of the child message starts with a substring matching the
  ** setting of $$reply_regexp.  With $$sort_re \fIunset\fP, mutt will attach
  ** the message whether or not this is the case, as long as the
  ** non-$$reply_regexp parts of both messages are identical.
  */
  { "sort_thread_groups", DT_SORT|DT_SORT_THREAD_GROUPS, R_INDEX|R_RESORT_BOTH, {.p=&SortThreadGroups}, {.l=SORT_AUX} },
  /*
  ** .pp
  ** When sorting by threads, this variable controls how threads are
  ** sorted in relation to other threads (at the top/root level).
  ** This can be set to any value that $$sort can, except ``threads''.
  ** You can also specify the ``last-'' prefix in addition to the
  ** ``reverse-'' prefix, but ``last-'' must come after ``reverse-''.
  ** The ``last-'' prefix causes messages to be sorted against its
  ** siblings by which has the last descendant, using the rest of
  ** $$sort_thread_groups as an ordering.
  ** .pp
  ** For backward compatibility, the default value is ``aux'', which
  ** means to use $$sort_aux for top-level thread sorting too.  The
  ** value ``aux'' does not respect ``last-'' or ``reverse-''
  ** prefixes, it simply delegates sorting directly to $$sort_aux.
  ** .pp
  ** Note: For reversed-threads $$sort order, $$sort_thread_groups is
  ** reversed again (which is not the right thing to do, but kept to
  ** not break any existing configuration setting).
  */
  { "spam_separator",   DT_STR, R_NONE, {.p=&SpamSep}, {.p=","} },
  /*
  ** .pp
  ** This variable controls what happens when multiple spam headers
  ** are matched: if \fIunset\fP, each successive header will overwrite any
  ** previous matches value for the spam label. If \fIset\fP, each successive
  ** match will append to the previous, using this variable's value as a
  ** separator.
  */
  { "spoolfile",	DT_PATH, R_NONE, {.p=&Spoolfile}, {.p=0} },
  /*
  ** .pp
  ** If your spool mailbox is in a non-default place where Mutt cannot find
  ** it, you can specify its location with this variable.  Mutt will
  ** initially set this variable to the value of the environment
  ** variable \fC$$$MAIL\fP or \fC$$$MAILDIR\fP if either is defined.
  */
#if defined(USE_SSL)
# ifdef USE_SSL_GNUTLS
  { "ssl_ca_certificates_file", DT_PATH, R_NONE, {.p=&SslCACertFile}, {.p=0} },
  /*
  ** .pp
  ** This variable specifies a file containing trusted CA certificates.
  ** Any server certificate that is signed with one of these CA
  ** certificates is also automatically accepted. (GnuTLS only)
  ** .pp
  ** Example:
  ** .ts
  ** set ssl_ca_certificates_file=/etc/ssl/certs/ca-certificates.crt
  ** .te
  */
# endif /* USE_SSL_GNUTLS */
  { "ssl_client_cert", DT_PATH, R_NONE, {.p=&SslClientCert}, {.p=0} },
  /*
  ** .pp
  ** The file containing a client certificate and its associated private
  ** key.
  */
  { "ssl_force_tls",		DT_BOOL, R_NONE, {.l=OPTSSLFORCETLS}, {.l=1} },
  /*
  ** .pp
  ** If this variable is \fIset\fP, Mutt will require that all connections
  ** to remote servers be encrypted. Furthermore it will attempt to
  ** negotiate TLS even if the server does not advertise the capability,
  ** since it would otherwise have to abort the connection anyway. This
  ** option supersedes $$ssl_starttls.
  */
# ifdef USE_SSL_GNUTLS
  { "ssl_min_dh_prime_bits", DT_NUM, R_NONE, {.p=&SslDHPrimeBits}, {.l=0} },
  /*
  ** .pp
  ** This variable specifies the minimum acceptable prime size (in bits)
  ** for use in any Diffie-Hellman key exchange. A value of 0 will use
  ** the default from the GNUTLS library. (GnuTLS only)
  */
# endif /* USE_SSL_GNUTLS */
  { "ssl_starttls", DT_QUAD, R_NONE, {.l=OPT_SSLSTARTTLS}, {.l=MUTT_YES} },
  /*
  ** .pp
  ** If \fIset\fP (the default), mutt will attempt to use \fCSTARTTLS\fP on servers
  ** advertising the capability. When \fIunset\fP, mutt will not attempt to
  ** use \fCSTARTTLS\fP regardless of the server's capabilities.
  ** .pp
  ** \fBNote\fP that \fCSTARTTLS\fP is subject to many kinds of
  ** attacks, including the ability of a machine-in-the-middle to
  ** suppress the advertising of support.  Setting $$ssl_force_tls is
  ** recommended if you rely on \fCSTARTTLS\fP.
  */
# ifdef USE_SSL_OPENSSL
  { "ssl_use_sslv2", DT_BOOL, R_NONE, {.l=OPTSSLV2}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP , Mutt will use SSLv2 when communicating with servers that
  ** request it. \fBN.B. As of 2011, SSLv2 is considered insecure, and using
  ** is inadvisable. See https://tools.ietf.org/html/rfc6176 .\fP
  ** (OpenSSL only)
  */
# endif /* defined USE_SSL_OPENSSL */
  { "ssl_use_sslv3", DT_BOOL, R_NONE, {.l=OPTSSLV3}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP , Mutt will use SSLv3 when communicating with servers that
  ** request it. \fBN.B. As of 2015, SSLv3 is considered insecure, and using
  ** it is inadvisable. See https://tools.ietf.org/html/rfc7525 .\fP
  */
  { "ssl_use_tlsv1", DT_BOOL, R_NONE, {.l=OPTTLSV1}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP , Mutt will use TLSv1.0 when communicating with servers that
  ** request it. \fBN.B. As of 2015, TLSv1.0 is considered insecure, and using
  ** it is inadvisable. See https://tools.ietf.org/html/rfc7525 .\fP
  */
  { "ssl_use_tlsv1_1", DT_BOOL, R_NONE, {.l=OPTTLSV1_1}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP , Mutt will use TLSv1.1 when communicating with servers that
  ** request it. \fBN.B. As of 2015, TLSv1.1 is considered insecure, and using
  ** it is inadvisable. See https://tools.ietf.org/html/rfc7525 .\fP
  */
  { "ssl_use_tlsv1_2", DT_BOOL, R_NONE, {.l=OPTTLSV1_2}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP , Mutt will use TLSv1.2 when communicating with servers that
  ** request it.
  */
  { "ssl_use_tlsv1_3", DT_BOOL, R_NONE, {.l=OPTTLSV1_3}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP , Mutt will use TLSv1.3 when communicating with servers that
  ** request it.
  */
#ifdef USE_SSL_OPENSSL
  { "ssl_usesystemcerts", DT_BOOL, R_NONE, {.l=OPTSSLSYSTEMCERTS}, {.l=1} },
  /*
  ** .pp
  ** If set to \fIyes\fP, mutt will use CA certificates in the
  ** system-wide certificate store when checking if a server certificate
  ** is signed by a trusted CA. (OpenSSL only)
  */
#endif
  { "ssl_verify_dates", DT_BOOL, R_NONE, {.l=OPTSSLVERIFYDATES}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP (the default), mutt will not automatically accept a server
  ** certificate that is either not yet valid or already expired. You should
  ** only unset this for particular known hosts, using the
  ** \fC$<account-hook>\fP function.
  */
  { "ssl_verify_host", DT_BOOL, R_NONE, {.l=OPTSSLVERIFYHOST}, {.l=1} },
  /*
  ** .pp
  ** If \fIset\fP (the default), mutt will not automatically accept a server
  ** certificate whose host name does not match the host used in your folder
  ** URL. You should only unset this for particular known hosts, using
  ** the \fC$<account-hook>\fP function.
  */
  { "ssl_verify_host_override", DT_STR, R_NONE, {.p=&SslVerifyHostOverride}, {.p=0} },
  /*
  ** .pp
  ** Defines an alternate host name to verify the server certificate against.
  ** This should not be set unless you are sure what you are doing, but it
  ** might be useful for connection to a .onion host without a properly
  ** configured host name in the certificate.  See $$ssl_verify_host.
  */
# ifdef USE_SSL_OPENSSL
#  ifdef HAVE_SSL_PARTIAL_CHAIN
  { "ssl_verify_partial_chains", DT_BOOL, R_NONE, {.l=OPTSSLVERIFYPARTIAL}, {.l=0} },
  /*
  ** .pp
  ** This option should not be changed from the default unless you understand
  ** what you are doing.
  ** .pp
  ** Setting this variable to \fIyes\fP will permit verifying partial
  ** certification chains, i. e. a certificate chain where not the root,
  ** but an intermediate certificate CA, or the host certificate, are
  ** marked trusted (in $$certificate_file), without marking the root
  ** signing CA as trusted.
  ** .pp
  ** (OpenSSL 1.0.2b and newer only).
  */
#  endif /* defined HAVE_SSL_PARTIAL_CHAIN */
# endif /* defined USE_SSL_OPENSSL */
  { "ssl_ciphers", DT_STR, R_NONE, {.p=&SslCiphers}, {.p=0} },
  /*
  ** .pp
  ** Contains a colon-separated list of ciphers to use when using SSL.
  ** For OpenSSL, see ciphers(1) for the syntax of the string.
  ** .pp
  ** For GnuTLS, this option will be used in place of "NORMAL" at the
  ** start of the priority string.  See gnutls_priority_init(3) for the
  ** syntax and more details. (Note: GnuTLS version 2.1.7 or higher is
  ** required.)
  */
#endif /* defined(USE_SSL) */
  { "status_chars",	DT_MBCHARTBL, R_BOTH, {.p=&StChars}, {.p="-*%A"} },
  /*
  ** .pp
  ** Controls the characters used by the ``%r'' indicator in
  ** $$status_format. The first character is used when the mailbox is
  ** unchanged. The second is used when the mailbox has been changed, and
  ** it needs to be resynchronized. The third is used if the mailbox is in
  ** read-only mode, or if the mailbox will not be written when exiting
  ** that mailbox (You can toggle whether to write changes to a mailbox
  ** with the \fC<toggle-write>\fP operation, bound by default to ``%''). The fourth
  ** is used to indicate that the current folder has been opened in attach-
  ** message mode (Certain operations like composing a new mail, replying,
  ** forwarding, etc. are not permitted in this mode).
  */
  /* L10N:
     $status_format default value
  */
  { "status_format", DT_STR|DT_L10N_STR, R_BOTH, {.p=&Status}, {.p=N_("-%r-Mutt: %f [Msgs:%?M?%M/?%m%?n? New:%n?%?o? Old:%o?%?d? Del:%d?%?F? Flag:%F?%?t? Tag:%t?%?p? Post:%p?%?b? Inc:%b?%?B? Back:%B?%?l? %l?]---(%s/%?T?%T/?%S)-%>-(%P)---")} },
  /*
  ** .pp
  ** Controls the format of the status line displayed in the ``index''
  ** menu.  This string is similar to $$index_format, but has its own
  ** set of \fCprintf(3)\fP-like sequences:
  ** .dl
  ** .dt %b  .dd number of mailboxes with new mail *
  ** .dt %B  .dd number of backgrounded editing sessions *
  ** .dt %d  .dd number of deleted messages *
  ** .dt %f  .dd the full pathname of the current mailbox
  ** .dt %F  .dd number of flagged messages *
  ** .dt %h  .dd local hostname
  ** .dt %l  .dd size (in bytes) of the current mailbox (see $formatstrings-size) *
  ** .dt %L  .dd size (in bytes) of the messages shown
  **             (i.e., which match the current limit) (see $formatstrings-size) *
  ** .dt %m  .dd the number of messages in the mailbox *
  ** .dt %M  .dd the number of messages shown (i.e., which match the current limit) *
  ** .dt %n  .dd number of new messages in the mailbox *
  ** .dt %o  .dd number of old unread messages *
  ** .dt %p  .dd number of postponed messages *
  ** .dt %P  .dd percentage of the way through the index
  ** .dt %r  .dd modified/read-only/won't-write/attach-message indicator,
  **             according to $$status_chars
  ** .dt %R  .dd number of read messages *
  ** .dt %s  .dd current sorting mode ($$sort)
  ** .dt %S  .dd current aux sorting method ($$sort_aux)
  ** .dt %t  .dd number of tagged messages *
  ** .dt %T  .dd current thread group sorting method ($$sort_thread_groups) *
  ** .dt %u  .dd number of unread messages *
  ** .dt %v  .dd Mutt version string
  ** .dt %V  .dd currently active limit pattern, if any *
  ** .dt %>X .dd right justify the rest of the string and pad with ``X''
  ** .dt %|X .dd pad to the end of the line with ``X''
  ** .dt %*X .dd soft-fill with character ``X'' as pad
  ** .de
  ** .pp
  ** For an explanation of ``soft-fill'', see the $$index_format documentation.
  ** .pp
  ** * = can be optionally printed if nonzero
  ** .pp
  ** Some of the above sequences can be used to optionally print a string
  ** if their value is nonzero.  For example, you may only want to see the
  ** number of flagged messages if such messages exist, since zero is not
  ** particularly meaningful.  To optionally print a string based upon one
  ** of the above sequences, the following construct is used:
  ** .pp
  **  \fC%?<sequence_char>?<optional_string>?\fP
  ** .pp
  ** where \fIsequence_char\fP is a character from the table above, and
  ** \fIoptional_string\fP is the string you would like printed if
  ** \fIsequence_char\fP is nonzero.  \fIoptional_string\fP \fBmay\fP contain
  ** other sequences as well as normal text, but you may \fBnot\fP nest
  ** optional strings.
  ** .pp
  ** Here is an example illustrating how to optionally print the number of
  ** new messages in a mailbox:
  ** .pp
  ** \fC%?n?%n new messages.?\fP
  ** .pp
  ** You can also switch between two strings using the following construct:
  ** .pp
  ** \fC%?<sequence_char>?<if_string>&<else_string>?\fP
  ** .pp
  ** If the value of \fIsequence_char\fP is non-zero, \fIif_string\fP will
  ** be expanded, otherwise \fIelse_string\fP will be expanded.
  ** .pp
  ** You can force the result of any \fCprintf(3)\fP-like sequence to be lowercase
  ** by prefixing the sequence character with an underscore (``_'') sign.
  ** For example, if you want to display the local hostname in lowercase,
  ** you would use: ``\fC%_h\fP''.
  ** .pp
  ** If you prefix the sequence character with a colon (``:'') character, mutt
  ** will replace any dots in the expansion by underscores. This might be helpful
  ** with IMAP folders that don't like dots in folder names.
  */
  { "status_on_top",	DT_BOOL, R_REFLOW, {.l=OPTSTATUSONTOP}, {.l=0} },
  /*
  ** .pp
  ** Setting this variable causes the ``status bar'' to be displayed on
  ** the first line of the screen rather than near the bottom. If $$help
  ** is \fIset\fP, too it'll be placed at the bottom.
  */
  { "strict_threads",	DT_BOOL, R_RESORT|R_RESORT_INIT|R_INDEX, {.l=OPTSTRICTTHREADS}, {.l=0} },
  /*
  ** .pp
  ** If \fIset\fP, threading will only make use of the ``In-Reply-To'' and
  ** ``References:'' fields when you $$sort by message threads.  By
  ** default, messages with the same subject are grouped together in
  ** ``pseudo threads.''. This may not always be desirable, such as in a
  ** personal mailbox where you might have several unrelated messages with
  ** the subjects like ``hi'' which will get grouped together. See also
  ** $$sort_re for a less drastic way of controlling this
  ** behavior.
  */
  { "suspend",		DT_BOOL, R_NONE, {.l=OPTSUSPEND}, {.l=1} },
  /*
  ** .pp
  ** When \fIunset\fP, mutt won't stop when the user presses the terminal's
  ** \fIsusp\fP key, usually ``^Z''. This is useful if you run mutt
  ** inside an xterm using a command like ``\fCxterm -e mutt\fP''.
  */
  { "text_flowed", 	DT_BOOL, R_NONE, {.l=OPTTEXTFLOWED},  {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will generate ``format=flowed'' bodies with a content type
  ** of ``\fCtext/plain; format=flowed\fP''.
  ** This format is easier to handle for some mailing software, and generally
  ** just looks like ordinary text.  To actually make use of this format's
  ** features, you'll need support in your editor.
  ** .pp
  ** The option only controls newly composed messages.  Postponed messages,
  ** resent messages, and draft messages (via -H on the command line) will
  ** use the content-type of the source message.
  ** .pp
  ** Note that $$indent_string is ignored when this option is \fIset\fP.
  */
  { "thorough_search",	DT_BOOL, R_NONE, {.l=OPTTHOROUGHSRC}, {.l=1} },
  /*
  ** .pp
  ** Affects the \fC~b\fP, \fC~B\fP, and \fC~h\fP search operations described in
  ** section ``$patterns''.  If \fIset\fP, the headers and body/attachments of
  ** messages to be searched are decoded before searching. If \fIunset\fP,
  ** messages are searched as they appear in the folder.
  ** .pp
  ** Users searching attachments or for non-ASCII characters should \fIset\fP
  ** this value because decoding also includes MIME parsing/decoding and possible
  ** character set conversions. Otherwise mutt will attempt to match against the
  ** raw message received (for example quoted-printable encoded or with encoded
  ** headers) which may lead to incorrect search results.
  */
  { "thread_received",	DT_BOOL, R_RESORT|R_RESORT_INIT|R_INDEX, {.l=OPTTHREADRECEIVED}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt uses the date received rather than the date sent
  ** to thread messages by subject.
  */
  { "tilde",		DT_BOOL, R_PAGER, {.l=OPTTILDE}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, the internal-pager will pad blank lines to the bottom of the
  ** screen with a tilde (``~'').
  */
  { "time_inc",		DT_NUM,	 R_NONE, {.p=&TimeInc}, {.l=0} },
  /*
  ** .pp
  ** Along with $$read_inc, $$write_inc, and $$net_inc, this
  ** variable controls the frequency with which progress updates are
  ** displayed. It suppresses updates less than $$time_inc milliseconds
  ** apart. This can improve throughput on systems with slow terminals,
  ** or when running mutt on a remote system.
  ** .pp
  ** Also see the ``$tuning'' section of the manual for performance considerations.
  */
  { "timeout",		DT_NUM,	 R_NONE, {.p=&Timeout}, {.l=600} },
  /*
  ** .pp
  ** When Mutt is waiting for user input either idling in menus or
  ** in an interactive prompt, Mutt would block until input is
  ** present. Depending on the context, this would prevent certain
  ** operations from working, like checking for new mail or keeping
  ** an IMAP connection alive.
  ** .pp
  ** This variable controls how many seconds Mutt will at most wait
  ** until it aborts waiting for input, performs these operations and
  ** continues to wait for input.
  ** .pp
  ** A value of zero or less will cause Mutt to never time out.
  */
  { "tmpdir",		DT_PATH, R_NONE, {.p=&Tempdir}, {.p=0} },
  /*
  ** .pp
  ** This variable allows you to specify where Mutt will place its
  ** temporary files needed for displaying and composing messages.  If
  ** this variable is not set, the environment variable \fC$$$TMPDIR\fP is
  ** used.  If \fC$$$TMPDIR\fP is not set then ``\fC/tmp\fP'' is used.
  */
  { "to_chars",		DT_MBCHARTBL, R_BOTH, {.p=&Tochars}, {.p=" +TCFL"} },
  /*
  ** .pp
  ** Controls the character used to indicate mail addressed to you.  The
  ** first character is the one used when the mail is \fInot\fP addressed to your
  ** address.  The second is used when you are the only
  ** recipient of the message.  The third is when your address
  ** appears in the ``To:'' header field, but you are not the only recipient of
  ** the message.  The fourth character is used when your
  ** address is specified in the ``Cc:'' header field, but you are not the only
  ** recipient.  The fifth character is used to indicate mail that was sent
  ** by \fIyou\fP.  The sixth character is used to indicate when a mail
  ** was sent to a mailing-list you subscribe to.
  */
  { "trash",		DT_PATH, R_NONE, {.p=&TrashPath}, {.p=0} },
  /*
  ** .pp
  ** If set, this variable specifies the path of the trash folder where the
  ** mails marked for deletion will be moved, instead of being irremediably
  ** purged.
  ** .pp
  ** NOTE: When you delete a message in the trash folder, it is really
  ** deleted, so that you have a way to clean the trash.
  */
  /* L10N:
     $ts_icon_format default value
  */
  {"ts_icon_format", DT_STR|DT_L10N_STR, R_BOTH, {.p=&TSIconFormat}, {.p=N_("M%?n?AIL&ail?")} },
  /*
  ** .pp
  ** Controls the format of the icon title, as long as ``$$ts_enabled'' is set.
  ** This string is identical in formatting to the one used by
  ** ``$$status_format''.
  */
  {"ts_enabled",	DT_BOOL,  R_BOTH, {.l=OPTTSENABLED}, {.l=0} },
  /* The default must be off to force in the validity checking. */
  /*
  ** .pp
  ** Controls whether mutt tries to set the terminal status line and icon name.
  ** Most terminal emulators emulate the status line in the window title.
  */
  /* L10N:
     $ts_status_format default value
  */
  {"ts_status_format", DT_STR|DT_L10N_STR, R_BOTH, {.p=&TSStatusFormat}, {.p=N_("Mutt with %?m?%m messages&no messages?%?n? [%n NEW]?")} },
  /*
  ** .pp
  ** Controls the format of the terminal status line (or window title),
  ** provided that ``$$ts_enabled'' has been set. This string is identical in
  ** formatting to the one used by ``$$status_format''.
  */
#ifdef USE_SOCKET
  { "tunnel",            DT_STR, R_NONE, {.p=&Tunnel}, {.p=0} },
  /*
  ** .pp
  ** Setting this variable will cause mutt to open a pipe to a command
  ** instead of a raw socket. You may be able to use this to set up
  ** preauthenticated connections to your IMAP/POP3/SMTP server. Example:
  ** .ts
  ** set tunnel="ssh -q mailhost.net /usr/local/libexec/imapd"
  ** .te
  ** .pp
  ** Note: For this example to work you must be able to log in to the remote
  ** machine without having to enter a password.
  ** .pp
  ** When set, Mutt uses the tunnel for all remote connections.
  ** Please see ``$account-hook'' in the manual for how to use different
  ** tunnel commands per connection.
  */
#endif
  { "tunnel_is_secure", DT_BOOL, R_NONE, {.l=OPTTUNNELISSECURE}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will assume the $$tunnel connection does not need
  ** STARTTLS to be enabled.  It will also allow IMAP PREAUTH server
  ** responses inside a $tunnel to proceed.  This is appropriate if $$tunnel
  ** uses ssh or directly invokes the server locally.
  ** .pp
  ** When \fIunset\fP, Mutt will negotiate STARTTLS according to the
  ** $ssl_starttls and $ssl_force_tls variables.  If $ssl_force_tls is
  ** set, Mutt will abort connecting if an IMAP server responds with PREAUTH.
  ** This setting is appropriate if $$tunnel does not provide security and
  ** could be tampered with by attackers.
  */
  { "uncollapse_jump", 	DT_BOOL, R_NONE, {.l=OPTUNCOLLAPSEJUMP}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will jump to the next unread message, if any,
  ** when the current thread is \fIun\fPcollapsed.
  */
  { "uncollapse_new", 	DT_BOOL, R_NONE, {.l=OPTUNCOLLAPSENEW}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will automatically uncollapse any collapsed
  ** thread that receives a newly delivered message.  When
  ** \fIunset\fP, collapsed threads will remain collapsed. The
  ** presence of the newly delivered message will still affect index
  ** sorting, though.
  */
  { "use_8bitmime",	DT_BOOL, R_NONE, {.l=OPTUSE8BITMIME}, {.l=0} },
  /*
  ** .pp
  ** \fBWarning:\fP do not set this variable unless you are using a version
  ** of sendmail which supports the \fC-B8BITMIME\fP flag (such as sendmail
  ** 8.8.x) or you may not be able to send mail.
  ** .pp
  ** When \fIset\fP, Mutt will invoke $$sendmail with the \fC-B8BITMIME\fP
  ** flag when sending 8-bit messages to enable ESMTP negotiation.
  */
  { "use_domain",	DT_BOOL, R_NONE, {.l=OPTUSEDOMAIN}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will qualify all local addresses (ones without the
  ** ``@host'' portion) with the value of $$hostname.  If \fIunset\fP, no
  ** addresses will be qualified.
  */
  { "use_envelope_from", 	DT_BOOL, R_NONE, {.l=OPTENVFROM}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will set the \fIenvelope\fP sender of the message.
  ** If $$envelope_from_address is \fIset\fP, it will be used as the sender
  ** address. If \fIunset\fP, mutt will attempt to derive the sender from the
  ** ``From:'' header.
  ** .pp
  ** Note that this information is passed to sendmail command using the
  ** \fC-f\fP command line switch. Therefore setting this option is not useful
  ** if the $$sendmail variable already contains \fC-f\fP or if the
  ** executable pointed to by $$sendmail doesn't support the \fC-f\fP switch.
  */
  { "envelope_from",	DT_SYN,  R_NONE, {.p="use_envelope_from"}, {.p=0} },
  /*
  */
  { "use_from",		DT_BOOL, R_NONE, {.l=OPTUSEFROM}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will generate the ``From:'' header field when
  ** sending messages.  If \fIunset\fP, no ``From:'' header field will be
  ** generated unless the user explicitly sets one using the ``$my_hdr''
  ** command.
  */
#ifdef HAVE_GETADDRINFO
  { "use_ipv6",		DT_BOOL, R_NONE, {.l=OPTUSEIPV6}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, Mutt will look for IPv6 addresses of hosts it tries to
  ** contact.  If this option is \fIunset\fP, Mutt will restrict itself to IPv4 addresses.
  ** Normally, the default should work.
  */
#endif /* HAVE_GETADDRINFO */
  { "user_agent",	DT_BOOL, R_NONE, {.l=OPTXMAILER}, {.l=0} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will add a ``User-Agent:'' header to outgoing
  ** messages, indicating which version of mutt was used for composing
  ** them.
  */
  { "visual",		DT_CMD_PATH, R_NONE, {.p=&Visual}, {.p=0} },
  /*
  ** .pp
  ** Specifies the visual editor to invoke when the ``\fC~v\fP'' command is
  ** given in the built-in editor.
  */
  { "wait_key",		DT_BOOL, R_NONE, {.l=OPTWAITKEY}, {.l=1} },
  /*
  ** .pp
  ** Controls whether Mutt will ask you to press a key after an external command
  ** has been invoked by these functions: \fC<shell-escape>\fP,
  ** \fC<pipe-message>\fP, \fC<pipe-entry>\fP, \fC<print-message>\fP,
  ** and \fC<print-entry>\fP commands.
  ** .pp
  ** It is also used when viewing attachments with ``$auto_view'', provided
  ** that the corresponding mailcap entry has a \fIneedsterminal\fP flag,
  ** and the external program is interactive.
  ** .pp
  ** When \fIset\fP, Mutt will always ask for a key. When \fIunset\fP, Mutt will wait
  ** for a key only if the external command returned a non-zero status.
  */
  { "weed",		DT_BOOL, R_NONE, {.l=OPTWEED}, {.l=1} },
  /*
  ** .pp
  ** When \fIset\fP, mutt will weed headers when displaying, forwarding,
  ** or replying to messages.
  ** .pp
  ** Also see $$copy_decode_weed, $$pipe_decode_weed, $$print_decode_weed.
  */
  { "wrap",             DT_NUM,  R_PAGER_FLOW, {.p=&Wrap}, {.l=0} },
  /*
  ** .pp
  ** When set to a positive value, mutt will wrap text at $$wrap characters.
  ** When set to a negative value, mutt will wrap text so that there are $$wrap
  ** characters of empty space on the right side of the terminal. Setting it
  ** to zero makes mutt wrap at the terminal width.
  ** .pp
  ** Also see $$reflow_wrap.
  */
  { "wrap_headers",     DT_NUM,  R_PAGER, {.p=&WrapHeaders}, {.l=78} },
  /*
  ** .pp
  ** This option specifies the number of characters to use for wrapping
  ** an outgoing message's headers. Allowed values are between 78 and 998
  ** inclusive.
  ** .pp
  ** \fBNote:\fP This option usually shouldn't be changed. RFC5233
  ** recommends a line length of 78 (the default), so \fBplease only change
  ** this setting when you know what you're doing\fP.
  */
  { "wrap_search",	DT_BOOL, R_NONE, {.l=OPTWRAPSEARCH}, {.l=1} },
  /*
  ** .pp
  ** Controls whether searches wrap around the end.
  ** .pp
  ** When \fIset\fP, searches will wrap around the first (or last) item. When
  ** \fIunset\fP, incremental searches will not wrap.
  */
  { "wrapmargin",	DT_NUM,	 R_PAGER_FLOW, {.p=&Wrap}, {.l=0} },
  /*
  ** .pp
  ** (DEPRECATED) Equivalent to setting $$wrap with a negative value.
  */
  { "write_bcc",	DT_BOOL, R_NONE, {.l=OPTWRITEBCC}, {.l=0} },
  /*
  ** .pp
  ** Controls whether mutt writes out the ``Bcc:'' header when
  ** preparing messages to be sent.  Some MTAs, such as Exim and
  ** Courier, do not strip the ``Bcc:'' header; so it is advisable to
  ** leave this unset unless you have a particular need for the header
  ** to be in the sent message.
  ** .pp
  ** If mutt is set to deliver directly via SMTP (see $$smtp_url),
  ** this option does nothing: mutt will never write out the ``Bcc:''
  ** header in this case.
  ** .pp
  ** Note this option only affects the sending of messages.  Fcc'ed
  ** copies of a message will always contain the ``Bcc:'' header if
  ** one exists.
  */
  { "write_inc",	DT_NUM,	 R_NONE, {.p=&WriteInc}, {.l=10} },
  /*
  ** .pp
  ** When writing a mailbox, a message will be printed every
  ** $$write_inc messages to indicate progress.  If set to 0, only a
  ** single message will be displayed before writing a mailbox.
  ** .pp
  ** Also see the $$read_inc, $$net_inc and $$time_inc variables and the
  ** ``$tuning'' section of the manual for performance considerations.
  */
  {"xterm_icon",	DT_SYN,  R_NONE, {.p="ts_icon_format"}, {.p=0} },
  /*
  */
  {"xterm_title",	DT_SYN,  R_NONE, {.p="ts_status_format"}, {.p=0} },
  /*
  */
  {"xterm_set_titles",	DT_SYN,  R_NONE, {.p="ts_enabled"}, {.p=0} },
  /*
  */
  /*--*/
  { NULL, 0, 0, {.l=0}, {.l=0} }
};


/* functions used to parse commands in a rc file */

static int parse_list (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_spam_list (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unlist (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
#ifdef USE_SIDEBAR
static int parse_path_list (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_path_unlist (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
#endif /* USE_SIDEBAR */

static int parse_group (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);

static int parse_lists (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unlists (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_alias (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unalias (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_echo (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_ignore (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unignore (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_run (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_source (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_cd (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_set (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_setenv (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_my_hdr (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unmy_hdr (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_subscribe (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unsubscribe (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_attachments (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unattachments (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);


static int parse_replace_list (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unreplace_list (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_subjectrx_list (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unsubjectrx_list (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_alternates (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
static int parse_unalternates (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);

/* Parse -group arguments */
static int parse_group_context (group_context_t **ctx, BUFFER *buf, BUFFER *s, BUFFER *err);


struct command_t
{
  char *name;
  int (*func) (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
  union pointer_long_t data;
};

const struct command_t Commands[] = {
  { "alternates",	parse_alternates,	{.l=0} },
  { "unalternates",	parse_unalternates,	{.l=0} },
#ifdef USE_SOCKET
  { "account-hook",     mutt_parse_hook,        {.l=MUTT_ACCOUNTHOOK} },
#endif
  { "alias",		parse_alias,		{.l=0} },
  { "attachments",	parse_attachments,	{.l=0} },
  { "unattachments",	parse_unattachments,	{.l=0} },
  { "auto_view",	parse_list,		{.p=&AutoViewList} },
  { "alternative_order",	parse_list,	{.p=&AlternativeOrderList} },
  { "bind",		mutt_parse_bind,	{.l=0} },
  { "cd",		parse_cd,		{.l=0} },
  { "charset-hook",	mutt_parse_hook,	{.l=MUTT_CHARSETHOOK} },
#ifdef HAVE_COLOR
  { "color",		mutt_parse_color,	{.l=0} },
  { "uncolor",		mutt_parse_uncolor,	{.l=0} },
#endif
  { "echo",		parse_echo,		{.l=0} },
  { "exec",		mutt_parse_exec,	{.l=0} },
  { "fcc-hook",		mutt_parse_hook,	{.l=MUTT_FCCHOOK} },
  { "fcc-save-hook",	mutt_parse_hook,	{.l=MUTT_FCCHOOK | MUTT_SAVEHOOK} },
  { "folder-hook",	mutt_parse_hook,	{.l=MUTT_FOLDERHOOK} },
#ifdef USE_COMPRESSED
  { "open-hook",	mutt_parse_hook,	{.l=MUTT_OPENHOOK} },
  { "close-hook",	mutt_parse_hook,	{.l=MUTT_CLOSEHOOK} },
  { "append-hook",	mutt_parse_hook,	{.l=MUTT_APPENDHOOK} },
#endif
  { "group",		parse_group,		{.l=MUTT_GROUP} },
  { "ungroup",		parse_group,		{.l=MUTT_UNGROUP} },
  { "hdr_order",	parse_list,		{.p=&HeaderOrderList} },
#ifdef HAVE_ICONV
  { "iconv-hook",	mutt_parse_hook,	{.l=MUTT_ICONVHOOK} },
#endif
  { "ignore",		parse_ignore,		{.l=0} },
  { "index-format-hook",mutt_parse_idxfmt_hook, {.l=MUTT_IDXFMTHOOK} },
  { "lists",		parse_lists,		{.l=0} },
  { "macro",		mutt_parse_macro,	{.l=0} },
  { "mailboxes",	mutt_parse_mailboxes,	{.l=0} },
  { "unmailboxes",	mutt_parse_unmailboxes,	{.l=0} },
  { "mailto_allow",	parse_list,		{.p=&MailtoAllow} },
  { "unmailto_allow",	parse_unlist,		{.p=&MailtoAllow} },
  { "message-hook",	mutt_parse_hook,	{.l=MUTT_MESSAGEHOOK} },
  { "mbox-hook",	mutt_parse_hook,	{.l=MUTT_MBOXHOOK} },
  { "mime_lookup",	parse_list,		{.p=&MimeLookupList} },
  { "unmime_lookup",	parse_unlist,		{.p=&MimeLookupList} },
  { "mono",		mutt_parse_mono,	{.l=0} },
  { "my_hdr",		parse_my_hdr,		{.l=0} },
  { "pgp-hook",		mutt_parse_hook,	{.l=MUTT_CRYPTHOOK} },
  { "crypt-hook",	mutt_parse_hook,	{.l=MUTT_CRYPTHOOK} },
  { "push",		mutt_parse_push,	{.l=0} },
  { "reply-hook",	mutt_parse_hook,	{.l=MUTT_REPLYHOOK} },
  { "reset",		parse_set,		{.l=MUTT_SET_RESET} },
  { "run",		parse_run,		{.l=0} },
  { "save-hook",	mutt_parse_hook,	{.l=MUTT_SAVEHOOK} },
  { "score",		mutt_parse_score,	{.l=0} },
  { "send-hook",	mutt_parse_hook,	{.l=MUTT_SENDHOOK} },
  { "send2-hook",	mutt_parse_hook,	{.l=MUTT_SEND2HOOK} },
  { "set",		parse_set,		{.l=0} },
  { "setenv",		parse_setenv,		{.l=0} },
#ifdef USE_SIDEBAR
  { "sidebar_whitelist",parse_path_list,	{.p=&SidebarWhitelist} },
  { "unsidebar_whitelist",parse_path_unlist,	{.p=&SidebarWhitelist} },
#endif
  { "source",		parse_source,		{.l=0} },
  { "spam",		parse_spam_list,	{.l=MUTT_SPAM} },
  { "nospam",		parse_spam_list,	{.l=MUTT_NOSPAM} },
  { "subscribe",	parse_subscribe,	{.l=0} },
  { "subjectrx",    parse_subjectrx_list, 	{.p=&SubjectRxList} },
  { "unsubjectrx",  parse_unsubjectrx_list,	{.p=&SubjectRxList} },
  { "toggle",		parse_set,		{.l=MUTT_SET_INV} },
  { "unalias",		parse_unalias,		{.l=0} },
  { "unalternative_order",parse_unlist,		{.p=&AlternativeOrderList} },
  { "unauto_view",	parse_unlist,		{.p=&AutoViewList} },
  { "unhdr_order",	parse_unlist,		{.p=&HeaderOrderList} },
  { "unhook",		mutt_parse_unhook,	{.l=0} },
  { "unignore",		parse_unignore,		{.l=0} },
  { "unlists",		parse_unlists,		{.l=0} },
  { "unmono",		mutt_parse_unmono,	{.l=0} },
  { "unmy_hdr",		parse_unmy_hdr,		{.l=0} },
  { "unscore",		mutt_parse_unscore,	{.l=0} },
  { "unset",		parse_set,		{.l=MUTT_SET_UNSET} },
  { "unsetenv",		parse_setenv,		{.l=MUTT_SET_UNSET} },
  { "unsubscribe",	parse_unsubscribe,	{.l=0} },
  { NULL,		NULL,			{.l=0} }
};
