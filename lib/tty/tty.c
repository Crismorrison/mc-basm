/*
   Interface to the terminal controlling library.

   Copyright (C) 2005, 2006, 2007, 2009 Free Software Foundation, Inc.

   Written by:
   Roland Illig <roland.illig@gmx.de>, 2005.
   Andrew Borodin <aborodin@vmail.ru>, 2009.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.
 */

/** \file tty.c
 *  \brief Source: %interface to the terminal controlling library
 */

#include <config.h>

#include <signal.h>
#include <stdarg.h>

#include "lib/global.h"
#include "lib/strutil.h"

#include "tty.h"
#include "tty-internal.h"


/*** global variables **************************************************/

/* If true lines are drown by spaces */
gboolean slow_tty = FALSE;

/* If true use +, -, | for line drawing */
gboolean ugly_line_drawing = FALSE;

int mc_tty_ugly_frm[MC_TTY_FRM_MAX];

/*** file scope macro definitions **************************************/

/*** file scope type declarations **************************************/

/*** file scope variables **********************************************/

static volatile sig_atomic_t got_interrupt = 0;

/*** file scope functions **********************************************/

static void
sigintr_handler (int signo)
{
    (void) &signo;
    got_interrupt = 1;
}

/*** public functions **************************************************/

extern gboolean
tty_is_slow (void)
{
    return slow_tty;
}

extern void
tty_start_interrupt_key (void)
{
    struct sigaction act;

    act.sa_handler = sigintr_handler;
    sigemptyset (&act.sa_mask);
    act.sa_flags = SA_RESTART;
    sigaction (SIGINT, &act, NULL);
}

extern void
tty_enable_interrupt_key (void)
{
    struct sigaction act;

    act.sa_handler = sigintr_handler;
    sigemptyset (&act.sa_mask);
    act.sa_flags = 0;
    sigaction (SIGINT, &act, NULL);
    got_interrupt = 0;
}

extern void
tty_disable_interrupt_key (void)
{
    struct sigaction act;

    act.sa_handler = SIG_IGN;
    sigemptyset (&act.sa_mask);
    act.sa_flags = 0;
    sigaction (SIGINT, &act, NULL);
}

extern gboolean
tty_got_interrupt (void)
{
    gboolean rv;

    rv = (got_interrupt != 0);
    got_interrupt = 0;
    return rv;
}

void
tty_print_one_hline (void)
{
    tty_print_alt_char (mc_tty_ugly_frm[MC_TTY_FRM_thinhoriz]);
}

void
tty_print_one_vline (void)
{
    tty_print_alt_char (mc_tty_ugly_frm[MC_TTY_FRM_thinvert]);
}

void
tty_draw_box (int y, int x, int ys, int xs)
{
    tty_draw_vline (y, x, mc_tty_ugly_frm[MC_TTY_FRM_vert], ys);
    tty_draw_vline (y, x + xs - 1, mc_tty_ugly_frm[MC_TTY_FRM_vert], ys);
    tty_draw_hline (y, x, mc_tty_ugly_frm[MC_TTY_FRM_horiz], xs);
    tty_draw_hline (y + ys - 1, x, mc_tty_ugly_frm[MC_TTY_FRM_horiz], xs);
    tty_gotoyx (y, x);
    tty_print_alt_char (mc_tty_ugly_frm[MC_TTY_FRM_lefttop]);
    tty_gotoyx (y + ys - 1, x);
    tty_print_alt_char (mc_tty_ugly_frm[MC_TTY_FRM_leftbottom]);
    tty_gotoyx (y, x + xs - 1);
    tty_print_alt_char (mc_tty_ugly_frm[MC_TTY_FRM_righttop]);
    tty_gotoyx (y + ys - 1, x + xs - 1);
    tty_print_alt_char (mc_tty_ugly_frm[MC_TTY_FRM_rightbottom]);
}

char *
mc_tty_normalize_from_utf8 (const char *str)
{
    GIConv conv;
    GString *buffer;
    const char *_system_codepage = str_detect_termencoding ();

    if (str_isutf8 (_system_codepage))
        return g_strdup (str);

    conv = g_iconv_open (_system_codepage, "UTF-8");
    if (conv == INVALID_CONV)
        return g_strdup (str);

    buffer = g_string_new ("");

    if (str_convert (conv, str, buffer) == ESTR_FAILURE) {
        g_string_free (buffer, TRUE);
        str_close_conv (conv);
        return g_strdup (str);
    }
    str_close_conv (conv);

    return g_string_free (buffer, FALSE);
}
