/* Editor key translation.

   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2006, 2007 Free Software Foundation, Inc.

   Authors: 1996, 1997 Paul Sheer

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/

/** \file
 *  \brief Source: editor key translation
 */

#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

#include <stdlib.h>

#include "lib/global.h"
#include "lib/tty/tty.h"	/* keys */
#include "lib/tty/key.h"	/* KEY_M_SHIFT */
#include "lib/strutil.h"	/* str_isutf8 () */

#include "edit-impl.h"
#include "edit-widget.h"	/* edit->macro_i */
#include "editcmd_dialogs.h"

#include "src/cmddef.h"	/* list of commands */
#include "src/keybind.h"	/* lookup_keymap_command() */
#include "src/charsets.h"	/* convert_from_input_c() */
#include "src/main.h"	/* display_codepage */

/*
 * Translate the keycode into either 'command' or 'char_for_insertion'.
 * 'command' is one of the editor commands from cmddef.h.
 */
int
edit_translate_key (WEdit *edit, long x_key, int *cmd, int *ch)
{
    unsigned long command = (unsigned long) CK_Insert_Char;
    int char_for_insertion = -1;
    int c;

    /* an ordinary insertable character */
    if (x_key < 256) {
#ifdef HAVE_CHARSET
        if ( edit->charpoint >= 4 ) {
            edit->charpoint = 0;
            edit->charbuf[edit->charpoint] = '\0';
        }
        if ( edit->charpoint < 4 ) {
            edit->charbuf[edit->charpoint++] = x_key;
            edit->charbuf[edit->charpoint] = '\0';
        }

        /* input from 8-bit locale */
        if ( !utf8_display ) {
            /* source in 8-bit codeset */
            if (!edit->utf8) {
#endif				/* HAVE_CHARSET */
                c = convert_from_input_c (x_key);
                if (is_printable (c)) {
                    char_for_insertion = c;
                    goto fin;
                }
#ifdef HAVE_CHARSET
            } else {
                c = convert_from_input_c (x_key);
                if (is_printable (c)) {
                    char_for_insertion = convert_from_8bit_to_utf_c2((unsigned char) x_key);
                    goto fin;
                }
            }
        /* UTF-8 locale */
        } else {
            /* source in UTF-8 codeset */
            if ( edit->utf8 ) {
                int res = str_is_valid_char (edit->charbuf, edit->charpoint);
                if (res < 0) {
                    if (res != -2) {
                        edit->charpoint = 0; /* broken multibyte char, skip */
                        goto fin;
                    }
                    char_for_insertion = x_key;
                    goto fin;
                } else {
                    edit->charbuf[edit->charpoint]='\0';
                    edit->charpoint = 0;
                    if ( g_unichar_isprint (g_utf8_get_char(edit->charbuf))) {
                        char_for_insertion = x_key;
                        goto fin;
                    }
                }

            /* 8-bit source */
            } else {
                int res = str_is_valid_char (edit->charbuf, edit->charpoint);
                if (res < 0) {
                    if (res != -2) {
                        edit->charpoint = 0; /* broken multibyte char, skip */
                        goto fin;
                    }
                    /* not finised multibyte input (in meddle multibyte utf-8 char) */
                    goto fin;
                } else {
                    if ( g_unichar_isprint (g_utf8_get_char(edit->charbuf)) ) {
                        c = convert_from_utf_to_current ( edit->charbuf );
                        edit->charbuf[0] = '\0';
                        edit->charpoint = 0;
                        char_for_insertion = c;
                        goto fin;
                    }
                    /* unprinteble utf input, skip it */
                    edit->charbuf[0] = '\0';
                    edit->charpoint = 0;
                }
            }
        }
#endif				/* HAVE_CHARSET */
    }

    /* Commands specific to the key emulation */
    if (edit->extmod) {
        edit->extmod = 0;
        command = lookup_keymap_command (editor_x_map, x_key);
    } else
        command = lookup_keymap_command (editor_map, x_key);

    if (command == CK_Ignore_Key)
        command = CK_Insert_Char;

  fin:
    *cmd = (int) command;		/* FIXME */
    *ch = char_for_insertion;

    return (command == (unsigned long) CK_Insert_Char && char_for_insertion == -1) ? 0 : 1;
}
