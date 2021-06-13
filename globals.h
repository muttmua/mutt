/*
 * Copyright (C) 1996-2002,2010,2016 Michael R. Elkins <me@mutt.org>
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

WHERE void (*mutt_error) (const char *, ...);
WHERE void (*mutt_message) (const char *, ...);

WHERE CONTEXT *Context;

WHERE char Errorbuf[STRING];
WHERE char AttachmentMarker[STRING];
WHERE char ProtectedHeaderMarker[STRING];

#if defined(DL_STANDALONE) && defined(USE_DOTLOCK)
WHERE char *MuttDotlock;
#endif

WHERE ADDRESS *EnvFrom;
WHERE ADDRESS *From;

WHERE char *AliasFile;
WHERE char *AliasFmt;
WHERE char *AssumedCharset;
WHERE char *AttachSaveDir;
WHERE char *AttachSep;
WHERE char *Attribution;
WHERE char *AttributionLocale;
WHERE char *AttachCharset;
WHERE char *AttachFormat;
#ifdef USE_AUTOCRYPT
WHERE char *AutocryptAcctFormat;
WHERE char *AutocryptDir;
WHERE char *AutocryptSignAs;  /* This is used in crypt-gpgme.c */
WHERE char *AutocryptDefaultKey;  /* Used for postponing messages */
#endif
WHERE char *BackgroundFormat;
WHERE char *Charset;
WHERE char *ComposeFormat;
WHERE char *ConfigCharset;
WHERE char *ContentType;
WHERE char *DefaultHook;
WHERE char *DateFmt;
WHERE char *DisplayFilter;
WHERE char *DsnNotify;
WHERE char *DsnReturn;
WHERE char *Editor;
WHERE char *EscChar;
WHERE char *FccDelimiter;
WHERE char *FolderFormat;
WHERE char *ForwardAttrIntro;
WHERE char *ForwardAttrTrailer;
WHERE char *ForwFmt;
WHERE char *Fqdn;
WHERE char *HdrFmt;
WHERE char *HistFile;
WHERE char *Homedir;
WHERE char *Hostname;
#ifdef USE_IMAP
WHERE char *ImapAuthenticators;
WHERE char *ImapDelimChars;
WHERE char *ImapHeaders;
WHERE char *ImapLogin;
WHERE char *ImapOauthRefreshCmd;
WHERE char *ImapPass;
WHERE char *ImapUser;
#endif
WHERE char *Inbox;
WHERE char *Ispell;
WHERE char *MailcapPath;
WHERE char *Maildir;
#if defined(USE_IMAP) || defined(USE_POP)
WHERE char *MessageCachedir;
#endif
#if USE_HCACHE
WHERE char *HeaderCache;
#if HAVE_GDBM || HAVE_DB4
WHERE long  HeaderCachePageSize;
#endif /* HAVE_GDBM || HAVE_DB4 */
#endif /* USE_HCACHE */
WHERE char *MarkMacroPrefix;
WHERE char *MessageIdFormat;
WHERE char *MhFlagged;
WHERE char *MhReplied;
WHERE char *MhUnseen;
WHERE char *MimeTypeQueryCmd;
WHERE char *MsgFmt;

#ifdef USE_SOCKET
WHERE char *Preconnect;
WHERE char *Tunnel;
WHERE short NetInc;
#endif /* USE_SOCKET */

#ifdef MIXMASTER
WHERE char *Mixmaster;
WHERE char *MixEntryFormat;
#endif

WHERE char *Muttrc;
WHERE char *Outbox;
WHERE char *Pager;
WHERE char *PagerFmt;
WHERE char *PatternFormat;
WHERE char *PipeSep;
#ifdef USE_POP
WHERE char *PopAuthenticators;
WHERE short PopCheckTimeout;
WHERE char *PopHost;
WHERE char *PopOauthRefreshCmd;
WHERE char *PopPass;
WHERE char *PopUser;
#endif
WHERE char *PostIndentString;
WHERE char *Postponed;
WHERE char *PostponeEncryptAs;
WHERE char *Prefix;
WHERE char *PrintCmd;
WHERE char *ProtHdrSubject;
WHERE char *NewMailCmd;
WHERE char *QueryCmd;
WHERE char *QueryFormat;
WHERE char *Realname;
WHERE short SearchContext;
WHERE char *SendCharset;
WHERE char *SendMultipartAltFilter;
WHERE char *Sendmail;
WHERE char *Shell;
#ifdef USE_SIDEBAR
WHERE char *SidebarDelimChars;
WHERE char *SidebarDividerChar;
WHERE char *SidebarFormat;
WHERE char *SidebarIndentString;
#endif
WHERE char *Signature;
WHERE char *SimpleSearch;
#if USE_SMTP
WHERE char *SmtpAuthenticators;
WHERE char *SmtpPass;
WHERE char *SmtpOauthRefreshCmd;
WHERE char *SmtpUrl;
#endif /* USE_SMTP */
WHERE char *Spoolfile;
WHERE char *SpamSep;
#if defined(USE_SSL)
WHERE char *SslCertFile;
WHERE char *SslClientCert;
WHERE char *SslEntropyFile;
WHERE char *SslCiphers;
#ifdef USE_SSL_GNUTLS
WHERE short SslDHPrimeBits;
WHERE char *SslCACertFile;
#endif
WHERE char *SslVerifyHostOverride;
#endif
WHERE mbchar_table *StChars;
WHERE char *Status;
WHERE char *Tempdir;
WHERE mbchar_table *Tochars;
WHERE char *TrashPath;
WHERE char *TSStatusFormat;
WHERE char *TSIconFormat;
WHERE short TSSupported;
WHERE char *Username;
WHERE char *Visual;

WHERE char *CurrentFolder;
WHERE char *LastFolder;


WHERE const char *ReleaseDate;

WHERE HASH *Groups;
WHERE HASH *ReverseAlias;
WHERE HASH *AutoSubscribeCache;

WHERE LIST *AutoViewList;
WHERE LIST *AlternativeOrderList;
WHERE LIST *AttachAllow;
WHERE LIST *AttachExclude;
WHERE LIST *InlineAllow;
WHERE LIST *InlineExclude;
WHERE LIST *RootAllow;
WHERE LIST *RootExclude;
WHERE LIST *HeaderOrderList;
WHERE LIST *Ignore;
WHERE LIST *MailtoAllow;
WHERE LIST *MimeLookupList;
WHERE LIST *UnIgnore;

WHERE RX_LIST *Alternates;
WHERE RX_LIST *UnAlternates;
WHERE RX_LIST *MailLists;
WHERE RX_LIST *UnMailLists;
WHERE RX_LIST *SubscribedLists;
WHERE RX_LIST *UnSubscribedLists;
WHERE REPLACE_LIST *SpamList;
WHERE RX_LIST *NoSpamList;
WHERE REPLACE_LIST *SubjectRxList;


/* bit vector for boolean variables */
#ifdef MAIN_C
unsigned char Options[(OPTMAX + 7)/8];
#else
extern unsigned char Options[];
#endif

/* bit vector for the yes/no/ask variable type */
#ifdef MAIN_C
unsigned char QuadOptions[(OPT_MAX*2 + 7) / 8];
#else
extern unsigned char QuadOptions[];
#endif

WHERE unsigned short Counter;

WHERE short ConnectTimeout;
WHERE short ErrorHistSize;
WHERE short HistSize;
WHERE short MenuContext;
WHERE short PagerContext;
WHERE short PagerIndexLines;
WHERE short PagerSkipQuotedContext;
WHERE short ReadInc;
WHERE short ReflowWrap;
WHERE short SaveHist;
WHERE short SendmailWait;
WHERE short SleepTime INITVAL (1);
WHERE short TimeInc;
WHERE short Timeout;
WHERE short Wrap;
WHERE short WrapHeaders;
WHERE short WriteInc;

WHERE short ScoreThresholdDelete;
WHERE short ScoreThresholdRead;
WHERE short ScoreThresholdFlag;

#ifdef USE_SIDEBAR
WHERE short SidebarWidth;
WHERE LIST *SidebarWhitelist;
#endif

#ifdef USE_IMAP
WHERE long  ImapFetchChunkSize;
WHERE short ImapKeepalive;
WHERE short ImapPipelineDepth;
WHERE short ImapPollTimeout;
#endif

/* flags for received signals */
WHERE SIG_ATOMIC_VOLATILE_T SigAlrm;
WHERE SIG_ATOMIC_VOLATILE_T SigInt;
WHERE SIG_ATOMIC_VOLATILE_T SigWinch;
WHERE SIG_ATOMIC_VOLATILE_T SigChld;

WHERE int CurrentMenu;

WHERE ALIAS *Aliases;
WHERE LIST *UserHeader;

/*-- formerly in pgp.h --*/
WHERE REGEXP PgpGoodSign;
WHERE REGEXP PgpDecryptionOkay;
WHERE char *PgpDefaultKey;
WHERE char *PgpSignAs;
WHERE long  PgpTimeout;
WHERE char *PgpEntryFormat;
WHERE char *PgpClearSignCommand;
WHERE char *PgpDecodeCommand;
WHERE char *PgpVerifyCommand;
WHERE char *PgpDecryptCommand;
WHERE char *PgpSignCommand;
WHERE char *PgpEncryptSignCommand;
WHERE char *PgpEncryptOnlyCommand;
WHERE char *PgpImportCommand;
WHERE char *PgpExportCommand;
WHERE char *PgpVerifyKeyCommand;
WHERE char *PgpListSecringCommand;
WHERE char *PgpListPubringCommand;
WHERE char *PgpGetkeysCommand;

/*-- formerly in smime.h --*/
WHERE char *SmimeDefaultKey;
WHERE char *SmimeSignAs;
WHERE long  SmimeTimeout;
WHERE char *SmimeCertificates;
WHERE char *SmimeKeys;
WHERE char *SmimeCryptAlg;
WHERE char *SmimeCALocation;
WHERE char *SmimeVerifyCommand;
WHERE char *SmimeVerifyOpaqueCommand;
WHERE char *SmimeDecryptCommand;
WHERE char *SmimeSignCommand;
WHERE char *SmimeDigestAlg;
WHERE char *SmimeSignOpaqueCommand;
WHERE char *SmimeEncryptCommand;
WHERE char *SmimeGetSignerCertCommand;
WHERE char *SmimePk7outCommand;
WHERE char *SmimeGetCertCommand;
WHERE char *SmimeImportCertCommand;
WHERE char *SmimeGetCertEmailCommand;




#ifdef MAIN_C
const char * const Weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char * const Months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", "ERR" };

const char * const BodyTypes[] = { "x-unknown", "audio", "application", "image", "message", "model", "multipart", "text", "video" };
const char * const BodyEncodings[] = { "x-unknown", "7bit", "8bit", "quoted-printable", "base64", "binary", "x-uuencoded" };
#else
extern const char * const Weekdays[];
extern const char * const Months[];
#endif

#ifdef MAIN_C
/* so that global vars get included */
#include "mx.h"
#include "mutt_regex.h"
#include "buffy.h"
#include "sort.h"
#include "mutt_crypt.h"
#include "reldate.h"
#endif /* MAIN_C */
