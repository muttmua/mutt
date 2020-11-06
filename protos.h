/*
 * Copyright (C) 1996-2000,2007,2010,2013 Michael R. Elkins <me@mutt.org>
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


#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif

#include "mbyte.h"

#define MoreArgs(p) (*p->dptr && *p->dptr != ';' && *p->dptr != '#')

#define mutt_make_string(A,B,C,D,E) _mutt_make_string(A,B,C,D,E,0)
void _mutt_make_string (char *, size_t, const char *, CONTEXT *,
                        HEADER *, format_flag);

struct hdr_format_info
{
  CONTEXT *ctx;
  HEADER *hdr;
  const char *pager_progress;
};

void mutt_make_string_info (char *, size_t, int, const char *, struct hdr_format_info *, format_flag);

int mutt_extract_token (BUFFER *, BUFFER *, int);
void mutt_free_opts (void);

#define mutt_system(x) _mutt_system(x,0)
int _mutt_system (const char *, int);

#define mutt_next_thread(x) _mutt_aside_thread(x,1,0)
#define mutt_previous_thread(x) _mutt_aside_thread(x,0,0)
#define mutt_next_subthread(x) _mutt_aside_thread(x,1,1)
#define mutt_previous_subthread(x) _mutt_aside_thread(x,0,1)
int _mutt_aside_thread (HEADER *, short, short);

#define mutt_collapse_thread(x,y) _mutt_traverse_thread (x,y,MUTT_THREAD_COLLAPSE)
#define mutt_uncollapse_thread(x,y) _mutt_traverse_thread (x,y,MUTT_THREAD_UNCOLLAPSE)
#define mutt_thread_contains_unread(x,y) _mutt_traverse_thread (x,y,MUTT_THREAD_UNREAD)
#define mutt_thread_next_unread(x,y) _mutt_traverse_thread(x,y,MUTT_THREAD_NEXT_UNREAD)
int _mutt_traverse_thread (CONTEXT *ctx, HEADER *hdr, int flag);


#define mutt_new_parameter() safe_calloc (1, sizeof (PARAMETER))
#define mutt_new_header() safe_calloc (1, sizeof (HEADER))
#define mutt_new_envelope() safe_calloc (1, sizeof (ENVELOPE))
#ifdef USE_AUTOCRYPT
#define mutt_new_autocrypthdr() safe_calloc (1, sizeof (AUTOCRYPTHDR))
#endif
#define mutt_new_enter_state() safe_calloc (1, sizeof (ENTER_STATE))

typedef const char * format_t (char *, size_t, size_t, int, char, const char *, const char *, const char *, const char *, void *, format_flag);

void mutt_FormatString (char *, size_t, size_t, int, const char *, format_t *, void *, format_flag);
void mutt_parse_content_type (char *, BODY *);
void mutt_generate_boundary (PARAMETER **);
void mutt_delete_parameter (const char *attribute, PARAMETER **p);
void mutt_set_parameter (const char *, const char *, PARAMETER **);


FILE *mutt_open_read (const char *, pid_t *);

void set_quadoption (int, int);
int query_quadoption (int, const char *);
int quadoption (int);

char* mutt_extract_message_id (const char *, const char **, int);

ADDRESS *mutt_get_address (ENVELOPE *, char **);
ADDRESS *mutt_lookup_alias (const char *s);
ADDRESS *mutt_remove_duplicates (ADDRESS *);
ADDRESS *mutt_expand_aliases (ADDRESS *);
ADDRESS *mutt_parse_adrlist (ADDRESS *, const char *);

BODY *mutt_run_send_alternative_filter (BODY *b);
BODY *mutt_make_file_attach (const char *);
BODY *mutt_make_message_attach (CONTEXT *, HEADER *, int);
BODY *mutt_remove_multipart (BODY *);
BODY *mutt_remove_multipart_mixed (BODY *);
BODY *mutt_make_multipart_mixed (BODY *);
BODY *mutt_make_multipart_alternative (BODY *b, BODY *alternative);
BODY *mutt_remove_multipart_alternative (BODY *b);
BODY *mutt_new_body (void);
BODY *mutt_parse_multipart (FILE *, const char *, LOFF_T, int);
BODY *mutt_parse_messageRFC822 (FILE *, BODY *);
BODY *mutt_read_mime_header (FILE *, int);

CONTENT *mutt_get_content_info (const char *fname, BODY *b);

HASH *mutt_make_id_hash (CONTEXT *);
HASH *mutt_make_subj_hash (CONTEXT *);

char *mutt_read_rfc822_line (FILE *, char *, size_t *);
ENVELOPE *mutt_read_rfc822_header (FILE *, HEADER *, short, short);
HEADER *mutt_dup_header (HEADER *);

void mutt_set_mtime (const char *from, const char *to);
time_t mutt_decrease_mtime (const char *, struct stat *);
time_t mutt_local_tz (time_t);
time_t mutt_mktime (struct tm *, int);
time_t mutt_parse_date (const char *, HEADER *);
time_t mutt_add_timeout (time_t, long);
int is_from (const char *, char *, size_t, time_t *);
void mutt_touch_atime (int);
int mutt_timespec_compare (struct timespec *a, struct timespec *b);
void mutt_get_stat_timespec (struct timespec *dest, struct stat *sb, mutt_stat_type type);
int mutt_stat_timespec_compare (struct stat *sba, mutt_stat_type type, struct timespec *b);
int mutt_stat_compare (struct stat *sba, mutt_stat_type sba_type, struct stat *sbb, mutt_stat_type sbb_type);


const char *mutt_attach_fmt (
  char *dest,
  size_t destlen,
  size_t col,
  int cols,
  char op,
  const char *src,
  const char *prefix,
  const char *ifstring,
  const char *elsestring,
  void *data,
  format_flag flags);


char *mutt_charset_hook (const char *);
char *mutt_iconv_hook (const char *);
void mutt_buffer_expand_path (BUFFER *);
void mutt_buffer_expand_path_norel (BUFFER *);
void _mutt_buffer_expand_path (BUFFER *, int, int);
void mutt_buffer_expand_multi_path (BUFFER *src, const char *delimiter);
void mutt_buffer_expand_multi_path_norel (BUFFER *src, const char *delimiter);
char *mutt_find_hook (int, const char *);
char *mutt_gecos_name (char *, size_t, struct passwd *);
char *mutt_gen_msgid (void);
char *mutt_get_body_charset (char *, size_t, BODY *);
const char *mutt_get_name (ADDRESS *);
char *mutt_get_parameter (const char *, PARAMETER *);
LIST *mutt_crypt_hook (ADDRESS *);
void mutt_make_date (BUFFER *);

const char *mutt_make_version (void);

const char *mutt_fqdn(short);

group_t *mutt_pattern_group (const char *);

REGEXP *mutt_compile_regexp (const char *, int);

void mutt_account_hook (const char* url);
void mutt_adv_mktemp (BUFFER *);
void mutt_alias_menu (char *, size_t, ALIAS *);
void mutt_allow_interrupt (int);
void mutt_auto_subscribe (const char *);
void mutt_block_signals (void);
void mutt_block_signals_system (void);
int mutt_body_handler (BODY *, STATE *);
int  mutt_bounce_message (FILE *fp, HEADER *, ADDRESS *);
void mutt_break_thread (HEADER *);
void mutt_browser_cleanup (void);
void mutt_buffer_concat_path (BUFFER *, const char *, const char *);
void mutt_buffer_concatn_path (BUFFER *dst, const char *dir, size_t dirlen,
                               const char *fname, size_t fnamelen);
#define mutt_buffer_quote_filename(a,b) _mutt_buffer_quote_filename (a, b, 1)
void _mutt_buffer_quote_filename (BUFFER *, const char *, int);
void mutt_buffer_sanitize_filename (BUFFER *d, const char *f, short slash);
void mutt_canonical_charset (char *, size_t, const char *);
void mutt_check_stats(void);
int mutt_count_body_parts (CONTEXT *, HEADER *);
void mutt_check_rescore (CONTEXT *);
void mutt_clear_error (void);
void mutt_clear_pager_position (void);
void mutt_commands_cleanup (void);
void mutt_create_alias (ENVELOPE *, ADDRESS *);
void mutt_decode_attachment (BODY *, STATE *);
void mutt_decode_base64 (STATE *s, long len, int istext, iconv_t cd);
void mutt_default_save (char *, size_t, HEADER *);
void mutt_display_address (ENVELOPE *);
void mutt_display_sanitize (char *);
int mutt_edit_content_type (HEADER *, BODY *, FILE *);
void mutt_edit_file (const char *, const char *);
int mutt_edit_headers (const char *, SEND_CONTEXT *, int);
char **mutt_envlist (void);
void mutt_envlist_set (const char *name, const char *value, int overwrite);
int mutt_filter_unprintable (char **);
int mutt_label_message (HEADER *);
void mutt_make_label_hash (CONTEXT *);
void mutt_label_hash_add (CONTEXT *ctx, HEADER *hdr);
void mutt_label_hash_remove (CONTEXT *ctx, HEADER *hdr);
int mutt_label_complete (char *, size_t, int);
void mutt_curses_error (const char *, ...);
void mutt_curses_message (const char *, ...);
void mutt_encode_path (BUFFER *, const char *);
void mutt_enter_command (void);
void mutt_error_history_display (void);
void mutt_error_history_init (void);
void mutt_expand_aliases_env (ENVELOPE *);
void mutt_expand_file_fmt (BUFFER *, const char *, const char *);
void mutt_expand_fmt (BUFFER *, const char *, const char *);
void mutt_folder_hook (const char *);
void mutt_format_string (char *, size_t, int, int, int, char, const char *, size_t, int);
void mutt_format_s (char *, size_t, const char *, const char *);
void mutt_format_s_tree (char *, size_t, const char *, const char *);
void mutt_free_alias (ALIAS **);
#ifdef USE_AUTOCRYPT
void mutt_free_autocrypthdr (AUTOCRYPTHDR **p);
#endif
void mutt_free_body (BODY **);
void mutt_free_color (int fg, int bg);
void mutt_free_enter_state (ENTER_STATE **);
void mutt_free_envelope (ENVELOPE **);
void mutt_free_header (HEADER **);
void mutt_free_parameter (PARAMETER **);
void mutt_free_regexp (REGEXP **);
void mutt_generate_header (char *, size_t, HEADER *, int);
const char *mutt_getcwd (BUFFER *);
void mutt_help (int);
const char *mutt_idxfmt_hook (const char *, CONTEXT *, HEADER *);
void mutt_draw_tree (CONTEXT *);
void mutt_check_lookup_list (BODY *, char *, size_t);
void mutt_make_help (char *, size_t, const char *, int, int);
void mutt_merge_envelopes(ENVELOPE* base, ENVELOPE** extra);
void mutt_message_to_7bit (BODY *, FILE *);
#define mutt_buffer_mktemp(a) mutt_buffer_mktemp_pfx_sfx (a, "mutt", NULL)
#define mutt_buffer_mktemp_pfx_sfx(a,b,c)  _mutt_buffer_mktemp (a, b, c, __FILE__, __LINE__)
void _mutt_buffer_mktemp (BUFFER *, const char *, const char *, const char *, int);
#define mutt_mktemp(a,b) mutt_mktemp_pfx_sfx (a, b, "mutt", NULL)
#define mutt_mktemp_pfx_sfx(a,b,c,d)  _mutt_mktemp (a, b, c, d, __FILE__, __LINE__)
void _mutt_mktemp (char *, size_t, const char *, const char *, const char *, int);
void mutt_normalize_time (struct tm *);
void mutt_paddstr (int, const char *);
void mutt_parse_mime_message (CONTEXT *ctx, HEADER *);
void mutt_parse_part (FILE *, BODY *);
void mutt_perror (const char *);
void mutt_prepare_envelope (ENVELOPE *, int);
void mutt_unprepare_envelope (ENVELOPE *);
void mutt_buffer_pretty_mailbox (BUFFER *);
void mutt_buffer_pretty_multi_mailbox (BUFFER *s, const char *delimiter);
void mutt_pretty_mailbox (char *, size_t);
void mutt_pretty_size (char *, size_t, LOFF_T);
void mutt_pipe_message (HEADER *);
void mutt_print_message (HEADER *);
void mutt_print_patchlist (void);
void mutt_query_exit (void);
void mutt_query_menu (char *, size_t);
void mutt_safe_path (BUFFER *dest, ADDRESS *a);
int mutt_rx_sanitize_string (BUFFER *dest, const char *src);
void mutt_save_path (char *s, size_t l, ADDRESS *a);
void mutt_buffer_save_path (BUFFER *dest, ADDRESS *a);
void mutt_score_message (CONTEXT *, HEADER *, int);
void mutt_select_fcc (BUFFER *, HEADER *);
#define mutt_select_file(A,B,C) _mutt_select_file(A,B,C,NULL,NULL)
void _mutt_select_file (char *, size_t, int, char ***, int *);
#define mutt_buffer_select_file(A,B) _mutt_buffer_select_file(A,B,NULL,NULL)
void _mutt_buffer_select_file (BUFFER *, int, char ***, int *);
void mutt_message_hook (CONTEXT *, HEADER *, int);
void _mutt_set_flag (CONTEXT *, HEADER *, int, int, int);
#define mutt_set_flag(a,b,c,d) _mutt_set_flag(a,b,c,d,1)
void mutt_shell_escape (void);
void mutt_show_error (void);
void mutt_signal_init (void);
void mutt_signal_cleanup (void);
void mutt_stamp_attachment (BODY *a);
void mutt_tabs_to_spaces (char *);
void mutt_tag_set_flag (int, int);
short mutt_ts_capability (void);
void mutt_unblock_signals (void);
void mutt_unblock_signals_system (int);
void mutt_update_encoding (BODY *a);
void mutt_version (void);
void mutt_view_attachments (HEADER *);
void mutt_write_address_list (ADDRESS *adr, FILE *fp, int linelen, int display);
void mutt_set_virtual (CONTEXT *);

int mutt_add_to_rx_list (RX_LIST **list, const char *s, int flags, BUFFER *err);
int mutt_addr_is_user (ADDRESS *);
int mutt_addwch (wchar_t);
int mutt_alias_complete (char *, size_t);
void mutt_alias_add_reverse (ALIAS *t);
void mutt_alias_delete_reverse (ALIAS *t);
void mutt_attrset_cursor (int source_pair, int cursor_pair);
int mutt_merge_colors (int source, int overlay);
int mutt_alloc_color (int fg, int bg, int ref);
int mutt_ask_pattern (char *, size_t);
int mutt_any_key_to_continue (const char *);
char *mutt_apply_replace (char *, size_t, char *, REPLACE_LIST *);
int mutt_builtin_editor (SEND_CONTEXT *);
int mutt_can_decode (BODY *);
int mutt_change_flag (HEADER *, int);
int mutt_check_encoding (const char *);
int mutt_check_key (const char *);
int mutt_check_menu (const char *);
int mutt_check_mime_type (const char *);
int mutt_check_month (const char *);
int mutt_check_overwrite (const char *, const char *, BUFFER *, int *, char **);
int mutt_check_traditional_pgp (HEADER *, int *);
int mutt_command_complete (char *, size_t, int, int);
int mutt_var_value_complete (char *, size_t, int);
int mutt_complete (char *, size_t);
int mutt_compose_attachment (BODY *a);
int mutt_copy_body (FILE *, BODY **, BODY *);
int mutt_decode_save_attachment (FILE *, BODY *, const char *, int, int);
int mutt_display_message (HEADER *h);
int mutt_dump_variables (void);
int mutt_edit_attachment(BODY *);
int mutt_edit_message (CONTEXT *, HEADER *);
int mutt_chscmp (const char *s, const char *chs);
#define mutt_is_utf8(a) mutt_chscmp (a, "utf-8")
#define mutt_is_us_ascii(a) mutt_chscmp (a, "us-ascii")
int mutt_parent_message (CONTEXT *, HEADER *, int);
int mutt_prepare_template(FILE*, CONTEXT *, HEADER *, HEADER *, short);
int mutt_enter_filename (const char *prompt, BUFFER *fname);
int mutt_enter_filenames (const char *prompt, char ***files, int *numfiles);
int mutt_enter_mailbox (const char *, BUFFER *, int);
int  mutt_enter_string (char *buf, size_t buflen, int col, int flags);
int _mutt_enter_string (char *, size_t, int, int, int, char ***, int *, ENTER_STATE *);
int mutt_get_field (const char *, char *, size_t, int);
int mutt_buffer_get_field (const char *, BUFFER *, int);
int mutt_get_hook_type (const char *);
int mutt_get_field_unbuffered (char *, char *, size_t, int);
#define mutt_get_password(A,B,C) mutt_get_field_unbuffered(A,B,C,MUTT_PASS)
int mutt_get_postponed (CONTEXT *, SEND_CONTEXT *);
int mutt_get_tmp_attachment (BODY *);
int mutt_index_menu (void);
int mutt_invoke_sendmail (ADDRESS *, ADDRESS *, ADDRESS *, ADDRESS *, const char *, int);
int mutt_is_mail_list (ADDRESS *);
int mutt_is_message_type(int, const char *);
int mutt_is_list_cc (int, ADDRESS *, ADDRESS *);
int mutt_is_list_recipient (int, ADDRESS *, ADDRESS *);
int mutt_is_quote_line (char *, regmatch_t *);
int mutt_is_subscribed_list (ADDRESS *);
int mutt_is_text_part (BODY *);
int mutt_is_valid_mailbox (const char *);
int mutt_link_threads (HEADER *, HEADER *, CONTEXT *);
int mutt_lookup_mime_type (BODY *, const char *);
int mutt_match_rx_list (const char *, RX_LIST *);
int mutt_match_spam_list (const char *, REPLACE_LIST *, char *, int);
int mutt_messages_in_thread (CONTEXT *, HEADER *, int);
int mutt_multi_choice (char *prompt, char *letters);
int mutt_needs_mailcap (BODY *);
int mutt_num_postponed (int);
int mutt_parse_bind (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_exec (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_color (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_uncolor (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_hook (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_idxfmt_hook (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_macro (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_mailboxes (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_unmailboxes (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_mono (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_unmono (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_push (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_rc_buffer (BUFFER *, BUFFER *, BUFFER *);
int mutt_parse_rc_line (const char *, BUFFER *);
LIST *mutt_parse_references (char *s, int allow_nb);
int mutt_parse_rfc822_line (ENVELOPE *e, HEADER *hdr, char *line, char *p,
                            short user_hdrs, short weed, short do_2047, LIST **lastp);
int mutt_parse_score (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_unscore (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_parse_unhook (BUFFER *, BUFFER *, union pointer_long_t, BUFFER *);
int mutt_pattern_func (int, char *);
int mutt_pipe_attachment (FILE *, BODY *, const char *, const char *);
int mutt_print_attachment (FILE *, BODY *);
int mutt_query_complete (char *, size_t);
int mutt_query_variables (LIST *queries);
int mutt_save_attachment (FILE *, BODY *, const char *, int, HEADER *);
int _mutt_save_message (HEADER *, CONTEXT *, int, int, int);
int mutt_save_message (HEADER *, int, int, int);
int mutt_search_command (int, int);
#ifdef USE_SMTP
int mutt_smtp_send (const ADDRESS *, const ADDRESS *, const ADDRESS *,
                    const ADDRESS *, const char *, int);
#endif
size_t mutt_wstr_trunc (const char *, size_t, size_t, size_t *);
int mutt_charlen (const char *s, int *);
int mutt_strwidth (const char *);
int mutt_compose_menu (SEND_CONTEXT *);
int mutt_thread_set_flag (HEADER *, int, int, int);
int mutt_user_is_recipient (HEADER *);
void mutt_update_num_postponed (void);
int mutt_wait_filter (pid_t);
int mutt_wait_interactive_filter (pid_t);
int mutt_which_case (const char *);
int mutt_write_fcc (const char *path, SEND_CONTEXT *sctx, const char *msgid, int, const char *);
int mutt_write_mime_body (BODY *, FILE *);
int mutt_write_mime_header (BODY *, FILE *);
int mutt_write_one_header (FILE *fp, const char *tag, const char *value, const char *pfx, int wraplen, int flags);
int mutt_write_rfc822_header (FILE *, ENVELOPE *, BODY *, char *, mutt_write_header_mode, int, int);
void mutt_write_references (LIST *, FILE *, int);
int mutt_yesorno (const char *, int);
void mutt_set_header_color(CONTEXT *, HEADER *);
void mutt_sleep (short);
int mutt_save_confirm (const char  *, struct stat *);

int mh_valid_message (const char *);

pid_t mutt_create_filter (const char *, FILE **, FILE **, FILE **);
pid_t mutt_create_filter_fd (const char *, FILE **, FILE **, FILE **, int, int, int);

ADDRESS *alias_reverse_lookup (ADDRESS *);

/* lib.c files transplanted to muttlib.c */
int mutt_rmtree (const char *);
FILE *safe_fopen (const char *, const char *);
int safe_open (const char *, int);
int safe_symlink (const char *, const char *);

/* base64.c */
void mutt_to_base64 (unsigned char*, const unsigned char*, size_t, size_t);
int mutt_from_base64 (char*, const char*, size_t);
void mutt_buffer_to_base64 (BUFFER *, const unsigned char *, size_t);
int mutt_buffer_from_base64 (BUFFER *, const char *);

/* utf8.c */
int mutt_wctoutf8 (char *s, unsigned int c, size_t buflen);

#ifdef LOCALES_HACK
#define IsPrint(c) (isprint((unsigned char)(c)) ||      \
                    ((unsigned char)(c) >= 0xa0))
#define IsWPrint(wc) (iswprint(wc) || wc >= 0xa0)
#else
#define IsPrint(c) (isprint((unsigned char)(c)) ||      \
                    (option (OPTLOCALES) ? 0 :          \
                     ((unsigned char)(c) >= 0xa0)))
#define IsWPrint(wc) (iswprint(wc) ||                           \
                      (option (OPTLOCALES) ? 0 : (wc >= 0xa0)))
#endif

#define new_pattern() safe_calloc(1, sizeof (pattern_t))

int mutt_pattern_exec (struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, HEADER *h, pattern_cache_t *);
pattern_t *mutt_pattern_comp (/* const */ char *s, int flags, BUFFER *err);
void mutt_check_simple (BUFFER *s, const char *simple);
void mutt_pattern_free (pattern_t **pat);

/* ----------------------------------------------------------------------------
 * Prototypes for broken systems
 */

#ifdef HAVE_LONG_LONG_INT
#ifdef LONGLONG
#error LONGLONG is already defined
#endif
#define LONGLONG long long
#else
#define LONGLONG long
#endif

#ifdef HAVE_SRAND48
#define LRAND lrand48
#define SRAND srand48
#define DRAND drand48
#else
#define LRAND rand
#define SRAND srand
#define DRAND (double)rand
#endif /* HAVE_SRAND48 */

/* HP-UX, ConvexOS and UNIXware don't have this macro */
#ifndef S_ISLNK
#define S_ISLNK(x) (((x) & S_IFMT) == S_IFLNK ? 1 : 0)
#endif

int getdnsdomainname (BUFFER *);

/* According to SCO support, this is how to detect SCO */
#if defined (_M_UNIX) || defined (MUTT_OS)
#define SCO
#endif

/* SCO Unix uses chsize() instead of ftruncate() */
#ifndef HAVE_FTRUNCATE
#define ftruncate chsize
#endif

#ifndef HAVE_SNPRINTF
extern int snprintf (char *, size_t, const char *, ...);
#endif

#ifndef HAVE_VSNPRINTF
extern int vsnprintf (char *, size_t, const char *, va_list);
#endif

#ifndef HAVE_STRERROR
#ifndef STDC_HEADERS
extern int sys_nerr;
extern char *sys_errlist[];
#endif

#define strerror(x) ((x) > 0 && (x) < sys_nerr) ? sys_errlist[(x)] : 0
#endif /* !HAVE_STRERROR */

#ifndef HAVE_MEMMOVE
#define memmove(d,s,n) bcopy((s),(d),(n))
#endif

/* AIX doesn't define these in any headers (sigh) */
int strcasecmp (const char *, const char *);
int strncasecmp (const char *, const char *, size_t);

#ifdef _AIX
int setegid (gid_t);
#endif /* _AIX */

#ifndef STDC_HEADERS
extern FILE *fdopen ();
extern int system ();
extern int puts ();
extern int fputs ();
extern int fputc ();
extern int fseek ();
extern char *strchr ();
extern int getopt ();
extern int fputs ();
extern int fputc ();
extern int fclose ();
extern int fprintf();
extern int printf ();
extern int fgetc ();
extern int tolower ();
extern int toupper ();
extern int sscanf ();
extern size_t fread ();
extern size_t fwrite ();
extern int system ();
extern int rename ();
extern time_t time ();
extern struct tm *localtime ();
extern char *asctime ();
extern char *strpbrk ();
extern int fflush ();
extern long lrand48 ();
extern void srand48 ();
extern time_t mktime ();
extern int vsprintf ();
extern int ungetc ();
extern int ftruncate ();
extern void *memset ();
extern int pclose ();
extern int socket ();
extern int connect ();
extern size_t strftime ();
extern int lstat ();
extern void rewind ();
extern int readlink ();

/* IRIX barfs on empty var decls because the system include file uses elipsis
   in the declaration.  So declare all the args to avoid compiler errors.  This
   should be harmless on other systems.  */
int ioctl (int, int, ...);

#endif

/* unsorted */
void ci_bounce_message (HEADER *);

/* prototypes for compatibility functions */

#ifndef HAVE_SETENV
int setenv (const char *, const char *, int);
#endif

#ifndef HAVE_STRCASECMP
int strcasecmp (char *, char *);
int strncasecmp (char *, char *, size_t);
#endif

#ifndef HAVE_STRDUP
char *strdup (const char *);
#endif

#ifndef HAVE_STRSEP
char *strsep (char **, const char *);
#endif

#ifndef HAVE_STRTOK_R
char *strtok_r (char *, const char *, char **);
#endif

#ifndef HAVE_WCSCASECMP
int wcscasecmp (const wchar_t *a, const wchar_t *b);
#endif

#ifndef HAVE_STRCASESTR
char *strcasestr (const char *, const char *);
#endif

#ifndef HAVE_MKDTEMP
char *mkdtemp (char *tmpl);
#endif
