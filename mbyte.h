#ifndef _MBYTE_H
# define _MBYTE_H

# ifndef HAVE_WCWIDTH
int wcwidth(wchar_t wc);
# endif /* !HAVE_WCWIDTH */

void mutt_set_charset(char *charset);
extern int Charset_is_utf8;
wchar_t replacement_char(void);
int is_display_corrupting_utf8(wchar_t wc);

#endif /* _MBYTE_H */
