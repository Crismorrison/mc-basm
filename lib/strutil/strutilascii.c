/* ASCII strings utilities
   Copyright (C) 2007 Free Software Foundation, Inc.

   Written 2007 by:
   Rostislav Benes 

   The file_date routine is mostly from GNU's fileutils package,
   written by Richard Stallman and David MacKenzie.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include <config.h>

#include <stdio.h>
#include <ctype.h>
#include <config.h>
#include <errno.h>
#include "lib/global.h"
#include "lib/strutil.h"

/* using g_ascii function from glib
 * on terminal are showed only ascii characters (lower then 0x80) 
 */

static const char replch = '?';

static void
str_ascii_insert_replace_char (GString * buffer)
{
    g_string_append_c (buffer, replch);
}

static int
str_ascii_is_valid_string (const char *text)
{
    (void) text;
    return 1;
}

static int
str_ascii_is_valid_char (const char *ch, size_t size)
{
    (void) ch;
    (void) size;
    return 1;
}

static void
str_ascii_cnext_char (const char **text)
{
    (*text)++;
}

static void
str_ascii_cprev_char (const char **text)
{
    (*text)--;
}

static int
str_ascii_cnext_noncomb_char (const char **text)
{
    if (*text[0] != '\0')
    {
	(*text)++;
	return 1;
    }
    else
	return 0;
}

static int
str_ascii_cprev_noncomb_char (const char **text, const char *begin)
{
    if ((*text) != begin)
    {
	(*text)--;
	return 1;
    }
    else
	return 0;
}

static int
str_ascii_isspace (const char *text)
{
    return g_ascii_isspace ((gchar) text[0]);
}

static int
str_ascii_ispunct (const char *text)
{
    return g_ascii_ispunct ((gchar) text[0]);
}

static int
str_ascii_isalnum (const char *text)
{
    return g_ascii_isalnum ((gchar) text[0]);
}

static int
str_ascii_isdigit (const char *text)
{
    return g_ascii_isdigit ((gchar) text[0]);
}

static int
str_ascii_isprint (const char *text)
{
    return g_ascii_isprint ((gchar) text[0]);
}

static int
str_ascii_iscombiningmark (const char *text)
{
    (void) text;
    return 0;
}

static int
str_ascii_toupper (const char *text, char **out, size_t * remain)
{
    if (*remain <= 1)
	return 0;
    (*out)[0] = (char) g_ascii_toupper ((gchar) text[0]);
    (*out)++;
    (*remain)--;
    return 1;
}

static int
str_ascii_tolower (const char *text, char **out, size_t * remain)
{
    if (*remain <= 1)
	return 0;
    (*out)[0] = (char) g_ascii_tolower ((gchar) text[0]);
    (*out)++;
    (*remain)--;
    return 1;
}

static int
str_ascii_length (const char *text)
{
    return strlen (text);
}

static int
str_ascii_length2 (const char *text, int size)
{
    return (size >= 0) ? min (strlen (text), (gsize) size) : strlen (text);
}

static gchar *
str_ascii_conv_gerror_message (GError *error, const char *def_msg)
{
    /* the same as str_utf8_conv_gerror_message() */
    if ((error != NULL) && (error->message != NULL))
	return g_strdup (error->message);

    return g_strdup (def_msg != NULL ? def_msg : "");
}

static estr_t
str_ascii_vfs_convert_to (GIConv coder, const char *string,
			  int size, GString * buffer)
{
    (void) coder;
    g_string_append_len (buffer, string, size);
    return ESTR_SUCCESS;
}


static const char *
str_ascii_term_form (const char *text)
{
    static char result[BUF_MEDIUM];
    char *actual;
    size_t remain;
    size_t length;
    size_t pos = 0;

    actual = result;
    remain = sizeof (result);
    length = strlen (text);

    /* go throw all characters and check, if they are ascii and printable */
    for (; pos < length && remain > 1; pos++, actual++, remain--)
    {
	actual[0] = isascii ((unsigned char) text[pos]) ? text[pos] : '?';
	actual[0] = g_ascii_isprint ((gchar) actual[0]) ? actual[0] : '.';
    }

    actual[0] = '\0';
    return result;
}

static const char *
str_ascii_fit_to_term (const char *text, int width, align_crt_t just_mode)
{
    static char result[BUF_MEDIUM];
    char *actual;
    size_t remain;
    int ident;
    size_t length;
    size_t pos = 0;

    length = strlen (text);
    actual = result;
    remain = sizeof (result);

    if ((int)length <= width)
    {
	ident = 0;
	switch (HIDE_FIT (just_mode))
	{
	case J_CENTER_LEFT:
	case J_CENTER:
	    ident = (width - length) / 2;
	    break;
	case J_RIGHT:
	    ident = width - length;
	    break;
	}

	/* add space before text */
	if ((int)remain <= ident)
	    goto finally;
	memset (actual, ' ', ident);
	actual += ident;
	remain -= ident;

	/* copy all characters */
	for (; pos < (gsize)length && remain > 1; pos++, actual++, remain--)
	{
	    actual[0] = isascii ((unsigned char) text[pos]) ? text[pos] : '?';
	    actual[0] = g_ascii_isprint ((gchar) actual[0]) ? actual[0] : '.';
	}

	/* add space after text */
	if (width - length - ident > 0)
	{
	    if (remain <= width - length - ident)
		goto finally;
	    memset (actual, ' ', width - length - ident);
	    actual += width - length - ident;
	    remain -= width - length - ident;
	}
    }
    else
    {
	if (IS_FIT (just_mode))
	{
	    /* copy prefix of text, that is not wider than width / 2 */
	    for (; pos + 1 <= (gsize)width / 2 && remain > 1;
		 actual++, pos++, remain--)
	    {
		actual[0] = isascii ((unsigned char) text[pos])
		    ? text[pos] : '?';
		actual[0] = g_ascii_isprint ((gchar) actual[0])
		    ? actual[0] : '.';
	    }

	    if (remain <= 1)
		goto finally;
	    actual[0] = '~';
	    actual++;
	    remain--;

	    pos += length - width + 1;

	    /* copy suffix of text */
	    for (; pos < length && remain > 1; pos++, actual++, remain--)
	    {
		actual[0] = isascii ((unsigned char) text[pos])
		    ? text[pos] : '?';
		actual[0] = g_ascii_isprint ((gchar) actual[0])
		    ? actual[0] : '.';
	    }
	}
	else
	{
	    ident = 0;
	    switch (HIDE_FIT (just_mode))
	    {
	    case J_CENTER:
		ident = (length - width) / 2;
		break;
	    case J_RIGHT:
		ident = length - width;
		break;
	    }

	    /* copy substring text, substring start from ident and take width 
	     * characters from text */
	    pos += ident;
	    for (; pos < (gsize)(ident + width) && remain > 1;
		 pos++, actual++, remain--)
	    {
		actual[0] = isascii ((unsigned char) text[pos])
		    ? text[pos] : '?';
		actual[0] = g_ascii_isprint ((gchar) actual[0])
		    ? actual[0] : '.';
	    }

	}
    }
  finally:
    actual[0] = '\0';
    return result;
}

static const char *
str_ascii_term_trim (const char *text, int width)
{
    static char result[BUF_MEDIUM];
    size_t remain;
    char *actual;
    size_t pos = 0;
    size_t length;

    length = strlen (text);
    actual = result;
    remain = sizeof (result);

    if (width < (int)length)
    {
	if (width <= 3)
	{
	    memset (actual, '.', width);
	    actual += width;
	    remain -= width;
	}
	else
	{
	    memset (actual, '.', 3);
	    actual += 3;
	    remain -= 3;

	    pos += length - width + 3;

	    /* copy suffix of text */
	    for (; pos < length && remain > 1; pos++, actual++, remain--)
	    {
		actual[0] = isascii ((unsigned char) text[pos])
		    ? text[pos] : '?';
		actual[0] = g_ascii_isprint ((gchar) actual[0])
		    ? actual[0] : '.';
	    }
	}
    }
    else
    {
	/* copy all characters */
	for (; pos < length && remain > 1; pos++, actual++, remain--)
	{
	    actual[0] = isascii ((unsigned char) text[pos]) ? text[pos] : '?';
	    actual[0] = g_ascii_isprint ((gchar) actual[0]) ? actual[0] : '.';
	}
    }

    actual[0] = '\0';
    return result;
}

static int
str_ascii_term_width2 (const char *text, size_t length)
{
    return (length != (size_t) (-1))
	? min (strlen (text), length) : strlen (text);
}

static int
str_ascii_term_width1 (const char *text)
{
    return str_ascii_term_width2 (text, (size_t) (-1));
}

static int
str_ascii_term_char_width (const char *text)
{
    (void) text;
    return 1;
}

static void
str_ascii_msg_term_size (const char *text, int *lines, int *columns)
{
    char *p, *tmp;
    char *q;
    char c = '\0';
    int width;

    (*lines) = 1;
    (*columns) = 0;

    tmp = g_strdup (text);
    p = tmp;

    for (;;)
    {
	q = strchr (p, '\n');
	if (q != NULL)
	{
	    c = q[0];
	    q[0] = '\0';
	}

	width = str_ascii_term_width1 (p);
	if (width > (*columns))
	    (*columns) = width;

	if (q == NULL)
	    break;
	q[0] = c;
	p = q + 1;
	(*lines)++;
    }
    g_free (tmp);
}

static const char *
str_ascii_term_substring (const char *text, int start, int width)
{
    static char result[BUF_MEDIUM];
    size_t remain;
    char *actual;
    size_t pos = 0;
    size_t length;

    actual = result;
    remain = sizeof (result);
    length = strlen (text);

    if (start < (int)length)
    {
	pos += start;
	/* copy at most width characters from text from start */
	for (; pos < length && width > 0 && remain > 1;
	     pos++, width--, actual++, remain--)
	{

	    actual[0] = isascii ((unsigned char) text[pos]) ? text[pos] : '?';
	    actual[0] = g_ascii_isprint ((gchar) actual[0]) ? actual[0] : '.';
	}
    }

    /* if text is shorter then width, add space to the end */
    for (; width > 0 && remain > 1; actual++, remain--, width--)
    {
	actual[0] = ' ';
    }

    actual[0] = '\0';
    return result;
}

static const char *
str_ascii_trunc (const char *text, int width)
{
    static char result[MC_MAXPATHLEN];
    int remain;
    char *actual;
    size_t pos = 0;
    size_t length;

    actual = result;
    remain = sizeof (result);
    length = strlen (text);

    if ((int)length > width)
    {
	/* copy prefix of text */
	for (; pos + 1 <= (gsize)width / 2 && remain > 1; actual++, pos++, remain--)
	{
	    actual[0] = isascii ((unsigned char) text[pos]) ? text[pos] : '?';
	    actual[0] = g_ascii_isprint ((gchar) actual[0]) ? actual[0] : '.';
	}

	if (remain <= 1)
	    goto finally;
	actual[0] = '~';
	actual++;
	remain--;

	pos += length - width + 1;

	/* copy suffix of text */
	for (; pos < length && remain > 1; pos++, actual++, remain--)
	{
	    actual[0] = isascii ((unsigned char) text[pos]) ? text[pos] : '?';
	    actual[0] = g_ascii_isprint ((gchar) actual[0]) ? actual[0] : '.';
	}
    }
    else
    {
	/* copy all characters */
	for (; pos < length && remain > 1; pos++, actual++, remain--)
	{
	    actual[0] = isascii ((unsigned char) text[pos]) ? text[pos] : '?';
	    actual[0] = g_ascii_isprint ((gchar) actual[0]) ? actual[0] : '.';
	}
    }

  finally:
    actual[0] = '\0';
    return result;
}

static int
str_ascii_offset_to_pos (const char *text, size_t length)
{
    (void) text;
    return (int) length;
}

static int
str_ascii_column_to_pos (const char *text, size_t pos)
{
    (void) text;
    return (int)pos;
}

static char *
str_ascii_create_search_needle (const char *needle, int case_sen)
{
    (void) case_sen;
    return (char *) needle;
}

static void
str_ascii_release_search_needle (char *needle, int case_sen)
{
    (void) case_sen;
    (void) needle;

}

static const char *
str_ascii_search_first (const char *text, const char *search, int case_sen)
{
    char *fold_text;
    char *fold_search;
    const char *match;
    size_t offset;

    fold_text = (case_sen) ? (char *) text : g_ascii_strdown (text, -1);
    fold_search = (case_sen) ? (char *) search : g_ascii_strdown (search, -1);

    match = g_strstr_len (fold_text, -1, fold_search);
    if (match != NULL)
    {
	offset = match - fold_text;
	match = text + offset;
    }

    if (!case_sen)
    {
	g_free (fold_text);
	g_free (fold_search);
    }

    return match;
}

static const char *
str_ascii_search_last (const char *text, const char *search, int case_sen)
{
    char *fold_text;
    char *fold_search;
    const char *match;
    size_t offset;

    fold_text = (case_sen) ? (char *) text : g_ascii_strdown (text, -1);
    fold_search = (case_sen) ? (char *) search : g_ascii_strdown (search, -1);

    match = g_strrstr_len (fold_text, -1, fold_search);
    if (match != NULL)
    {
	offset = match - fold_text;
	match = text + offset;
    }

    if (!case_sen)
    {
	g_free (fold_text);
	g_free (fold_search);
    }

    return match;
}

static int
str_ascii_compare (const char *t1, const char *t2)
{
    return strcmp (t1, t2);
}

static int
str_ascii_ncompare (const char *t1, const char *t2)
{
    return strncmp (t1, t2, min (strlen (t1), strlen (t2)));
}

static int
str_ascii_casecmp (const char *t1, const char *t2)
{
    return g_ascii_strcasecmp (t1, t2);
}

static int
str_ascii_ncasecmp (const char *t1, const char *t2)
{
    return g_ascii_strncasecmp (t1, t2, min (strlen (t1), strlen (t2)));
}

static void
str_ascii_fix_string (char *text)
{
    for (; text[0] != '\0'; text++)
    {
	text[0] = ((unsigned char) text[0] < 128) ? text[0] : '?';
    }
}

static char *
str_ascii_create_key (const char *text, int case_sen)
{
    (void) case_sen;
    return (char *) text;
}

static int
str_ascii_key_collate (const char *t1, const char *t2, int case_sen)
{
    return (case_sen) ? strcmp (t1, t2) : g_ascii_strcasecmp (t1, t2);
}

static void
str_ascii_release_key (char *key, int case_sen)
{
    (void) key;
    (void) case_sen;
}

static int
str_ascii_prefix (const char *text, const char *prefix)
{
    int result;
    for (result = 0; text[result] != '\0' && prefix[result] != '\0'
	 && text[result] == prefix[result]; result++);
    return result;
}

static int
str_ascii_caseprefix (const char *text, const char *prefix)
{
    int result;
    for (result = 0; text[result] != '\0' && prefix[result] != '\0'
	 && g_ascii_toupper (text[result]) ==
	 g_ascii_toupper (prefix[result]); result++);
    return result;
}


struct str_class
str_ascii_init (void)
{
    struct str_class result;

    result.conv_gerror_message = str_ascii_conv_gerror_message;
    result.vfs_convert_to = str_ascii_vfs_convert_to;
    result.insert_replace_char = str_ascii_insert_replace_char;
    result.is_valid_string = str_ascii_is_valid_string;
    result.is_valid_char = str_ascii_is_valid_char;
    result.cnext_char = str_ascii_cnext_char;
    result.cprev_char = str_ascii_cprev_char;
    result.cnext_char_safe = str_ascii_cnext_char;
    result.cprev_char_safe = str_ascii_cprev_char;
    result.cnext_noncomb_char = str_ascii_cnext_noncomb_char;
    result.cprev_noncomb_char = str_ascii_cprev_noncomb_char;
    result.isspace = str_ascii_isspace;
    result.ispunct = str_ascii_ispunct;
    result.isalnum = str_ascii_isalnum;
    result.isdigit = str_ascii_isdigit;
    result.isprint = str_ascii_isprint;
    result.iscombiningmark = str_ascii_iscombiningmark;
    result.toupper = str_ascii_toupper;
    result.tolower = str_ascii_tolower;
    result.length = str_ascii_length;
    result.length2 = str_ascii_length2;
    result.length_noncomb = str_ascii_length;
    result.fix_string = str_ascii_fix_string;
    result.term_form = str_ascii_term_form;
    result.fit_to_term = str_ascii_fit_to_term;
    result.term_trim = str_ascii_term_trim;
    result.term_width2 = str_ascii_term_width2;
    result.term_width1 = str_ascii_term_width1;
    result.term_char_width = str_ascii_term_char_width;
    result.msg_term_size = str_ascii_msg_term_size;
    result.term_substring = str_ascii_term_substring;
    result.trunc = str_ascii_trunc;
    result.offset_to_pos = str_ascii_offset_to_pos;
    result.column_to_pos = str_ascii_column_to_pos;
    result.create_search_needle = str_ascii_create_search_needle;
    result.release_search_needle = str_ascii_release_search_needle;
    result.search_first = str_ascii_search_first;
    result.search_last = str_ascii_search_last;
    result.compare = str_ascii_compare;
    result.ncompare = str_ascii_ncompare;
    result.casecmp = str_ascii_casecmp;
    result.ncasecmp = str_ascii_ncasecmp;
    result.prefix = str_ascii_prefix;
    result.caseprefix = str_ascii_caseprefix;
    result.create_key = str_ascii_create_key;
    result.create_key_for_filename = str_ascii_create_key;
    result.key_collate = str_ascii_key_collate;
    result.release_key = str_ascii_release_key;

    return result;
}
