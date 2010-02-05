/* Widgets for the Midnight Commander

   Copyright (C) 1994, 1995, 1996, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006, 2007, 2009 Free Software Foundation, Inc.

   Authors: 1994, 1995 Radek Doulik
            1994, 1995 Miguel de Icaza
            1995 Jakub Jelinek
	    1996 Andrej Borsenkow
	    1997 Norbert Warmuth

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

/** \file widget.c
 *  \brief Source: widgets
 */

#include <config.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>

#include "lib/global.h"

#include "lib/tty/tty.h"
#include "lib/tty/mouse.h"
#include "lib/tty/key.h"	/* XCTRL and ALT macros  */
#include "lib/skin.h"
#include "lib/mcconfig.h"	/* for history loading and saving */
#include "lib/vfs/mc-vfs/vfs.h"
#include "lib/fileloc.h"
#include "lib/strutil.h"

#include "dialog.h"
#include "widget.h"
#include "wtools.h"

#include "cmddef.h"		/* CK_ cmd name const */
#include "keybind.h"		/* global_keymap_t */
#include "panel.h"		/* current_panel */
#include "main.h"		/* confirm_history_cleanup */

const global_keymap_t *input_map;

static void
widget_selectcolor (Widget *w, gboolean focused, gboolean hotkey)
{
    Dlg_head *h = w->parent;

    tty_setcolor (hotkey
	? (focused
	    ? DLG_HOT_FOCUSC (h)
	    : DLG_HOT_NORMALC (h))
	: (focused
	    ? DLG_FOCUSC (h)
	    : DLG_NORMALC (h)));
}

struct hotkey_t
parse_hotkey (const char *text)
{
    struct hotkey_t result;
    const char *cp, *p;

    /* search for '&', that is not on the of text */
    cp = strchr (text, '&');
    if (cp != NULL && cp[1] != '\0') {
        result.start = g_strndup (text, cp - text);

        /* skip '&' */
        cp++;
        p = str_cget_next_char (cp);
        result.hotkey = g_strndup (cp, p - cp);

        cp = p;
        result.end = g_strdup (cp);
    } else {
        result.start = g_strdup (text);
        result.hotkey = NULL;
        result.end = NULL;
    }

    return result;
}
void
release_hotkey (const struct hotkey_t hotkey)
{
    g_free (hotkey.start);
    g_free (hotkey.hotkey);
    g_free (hotkey.end);
}

int
hotkey_width (const struct hotkey_t hotkey)
{
    int result;

    result = str_term_width1 (hotkey.start);
    result+= (hotkey.hotkey != NULL) ? str_term_width1 (hotkey.hotkey) : 0;
    result+= (hotkey.end != NULL) ? str_term_width1 (hotkey.end) : 0;
    return result;
}

static void
draw_hotkey (Widget *w, const struct hotkey_t hotkey, gboolean focused)
{
    widget_selectcolor (w, focused, FALSE);
    tty_print_string (hotkey.start);

    if (hotkey.hotkey != NULL) {
	widget_selectcolor (w, focused, TRUE);
	tty_print_string (hotkey.hotkey);
	widget_selectcolor (w, focused, FALSE);
    }

    if (hotkey.end != NULL)
	tty_print_string (hotkey.end);
}


/* Default callback for widgets */
cb_ret_t
default_proc (widget_msg_t msg, int parm)
{
    (void) parm;

    switch (msg) {
    case WIDGET_INIT:
    case WIDGET_FOCUS:
    case WIDGET_UNFOCUS:
    case WIDGET_DRAW:
    case WIDGET_DESTROY:
    case WIDGET_CURSOR:
    case WIDGET_IDLE:
	return MSG_HANDLED;

    default:
	return MSG_NOT_HANDLED;
    }
}

static int button_event (Gpm_Event *event, void *);

int quote = 0;

static cb_ret_t
button_callback (Widget *w, widget_msg_t msg, int parm)
{
    WButton *b = (WButton *) w;
    int stop = 0;
    int off = 0;
    Dlg_head *h = b->widget.parent;

    switch (msg) {
    case WIDGET_HOTKEY:
	/*
	 * Don't let the default button steal Enter from the current
	 * button.  This is a workaround for the flawed event model
	 * when hotkeys are sent to all widgets before the key is
	 * handled by the current widget.
	 */
	if (parm == '\n' && h->current == &b->widget) {
	    button_callback (w, WIDGET_KEY, ' ');
	    return MSG_HANDLED;
	}

	if (parm == '\n' && b->flags == DEFPUSH_BUTTON) {
	    button_callback (w, WIDGET_KEY, ' ');
	    return MSG_HANDLED;
	}

        if (b->text.hotkey != NULL) {
            if (g_ascii_tolower ((gchar)b->text.hotkey[0]) ==
                g_ascii_tolower ((gchar)parm)) {
		button_callback (w, WIDGET_KEY, ' ');
		return MSG_HANDLED;
	    }
        }
	return MSG_NOT_HANDLED;

    case WIDGET_KEY:
	if (parm != ' ' && parm != '\n')
	    return MSG_NOT_HANDLED;

	if (b->callback)
	    stop = (*b->callback) (b->action);
	if (!b->callback || stop) {
	    h->ret_value = b->action;
	    dlg_stop (h);
	}
	return MSG_HANDLED;

    case WIDGET_CURSOR:
	switch (b->flags) {
	case DEFPUSH_BUTTON:
	    off = 3;
	    break;
	case NORMAL_BUTTON:
	    off = 2;
	    break;
	case NARROW_BUTTON:
	    off = 1;
	    break;
	case HIDDEN_BUTTON:
	default:
	    off = 0;
	    break;
	}
	widget_move (&b->widget, 0, b->hotpos + off);
	return MSG_HANDLED;

    case WIDGET_UNFOCUS:
    case WIDGET_FOCUS:
    case WIDGET_DRAW:
	if (msg == WIDGET_UNFOCUS)
	    b->selected = 0;
	else if (msg == WIDGET_FOCUS)
	    b->selected = 1;

        widget_selectcolor (w, b->selected, FALSE);
        widget_move (w, 0, 0);

	switch (b->flags) {
	case DEFPUSH_BUTTON:
                tty_print_string ("[< ");
	    break;
	case NORMAL_BUTTON:
                tty_print_string ("[ ");
	    break;
	case NARROW_BUTTON:
                tty_print_string ("[");
	    break;
	case HIDDEN_BUTTON:
	default:
                return MSG_HANDLED;
	}

	draw_hotkey (w, b->text, b->selected);

        switch (b->flags) {
            case DEFPUSH_BUTTON:
                tty_print_string (" >]");
                break;
            case NORMAL_BUTTON:
                tty_print_string (" ]");
                break;
            case NARROW_BUTTON:
                tty_print_string ("]");
                break;
	}
	return MSG_HANDLED;

    case WIDGET_DESTROY:
        release_hotkey (b->text);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

static int
button_event (Gpm_Event *event, void *data)
{
    WButton *b = data;

    if (event->type & (GPM_DOWN|GPM_UP)){
	Dlg_head *h = b->widget.parent;
	dlg_select_widget (b);
	if (event->type & GPM_UP){
	    button_callback ((Widget *) data, WIDGET_KEY, ' ');
	    h->callback (h, &b->widget, DLG_POST_KEY, ' ', NULL);
	    return MOU_NORMAL;
	}
    }
    return MOU_NORMAL;
}

int
button_get_len (const WButton *b)
{
    int ret = hotkey_width (b->text);
    switch (b->flags) {
	case DEFPUSH_BUTTON:
	    ret += 6;
	    break;
	case NORMAL_BUTTON:
	    ret += 4;
	    break;
	case NARROW_BUTTON:
	    ret += 2;
	    break;
	case HIDDEN_BUTTON:
	default:
	    return 0;
    }
    return ret;
}

WButton *
button_new (int y, int x, int action, int flags, const char *text,
	    bcback callback)
{
    WButton *b = g_new (WButton, 1);

    b->action = action;
    b->flags  = flags;
    b->text = parse_hotkey (text);

    init_widget (&b->widget, y, x, 1, button_get_len (b),
		 button_callback, button_event);

    b->selected = 0;
    b->callback = callback;
    widget_want_hotkey (b->widget, 1);
    b->hotpos = (b->text.hotkey != NULL) ? str_term_width1 (b->text.start) : -1;

    return b;
}

const char *
button_get_text (const WButton *b)
{
    if (b->text.hotkey != NULL)
        return g_strconcat (b->text.start, "&", b->text.hotkey,
                            b->text.end, (char *) NULL);
    else
        return g_strdup (b->text.start);
}

void
button_set_text (WButton *b, const char *text)
{
    release_hotkey (b->text);
    b->text = parse_hotkey (text);
    b->widget.cols = button_get_len (b);
    dlg_redraw (b->widget.parent);
}


/* Radio button widget */
static int radio_event (Gpm_Event *event, void *);

static cb_ret_t
radio_callback (Widget *w, widget_msg_t msg, int parm)
{
    WRadio *r = (WRadio *) w;
    int i;
    Dlg_head *h = r->widget.parent;

    switch (msg) {
    case WIDGET_HOTKEY:
	{
	    int lp = g_ascii_tolower ((gchar)parm);

	    for (i = 0; i < r->count; i++) {
                if (r->texts[i].hotkey != NULL) {
                    int c = g_ascii_tolower ((gchar)r->texts[i].hotkey[0]);

		    if (c != lp)
			continue;
		    r->pos = i;

		    /* Take action */
		    radio_callback (w, WIDGET_KEY, ' ');
		    return MSG_HANDLED;
		}
	    }
	}
	return MSG_NOT_HANDLED;

    case WIDGET_KEY:
	switch (parm) {
	case ' ':
	    r->sel = r->pos;
	    h->callback (h, w, DLG_ACTION, 0, NULL);
	    radio_callback (w, WIDGET_FOCUS, ' ');
	    return MSG_HANDLED;

	case KEY_UP:
	case KEY_LEFT:
	    if (r->pos > 0) {
		r->pos--;
		return MSG_HANDLED;
	    }
	    return MSG_NOT_HANDLED;

	case KEY_DOWN:
	case KEY_RIGHT:
	    if (r->count - 1 > r->pos) {
		r->pos++;
		return MSG_HANDLED;
	    }
	}
	return MSG_NOT_HANDLED;

    case WIDGET_CURSOR:
	h->callback (h, w, DLG_ACTION, 0, NULL);
	radio_callback (w, WIDGET_FOCUS, ' ');
	widget_move (&r->widget, r->pos, 1);
	return MSG_HANDLED;

    case WIDGET_UNFOCUS:
    case WIDGET_FOCUS:
    case WIDGET_DRAW:
	for (i = 0; i < r->count; i++) {
	    const gboolean focused = (i == r->pos && msg == WIDGET_FOCUS);
	    widget_selectcolor (w, focused, FALSE);
	    widget_move (&r->widget, i, 0);
	    tty_print_string ((r->sel == i) ? "(*) " : "( ) ");
	    draw_hotkey (w, r->texts[i], focused);
	}
	return MSG_HANDLED;

    case WIDGET_DESTROY:
        for (i = 0; i < r->count; i++) {
            release_hotkey (r->texts[i]);
        }
        g_free (r->texts);
        return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

static int
radio_event (Gpm_Event *event, void *data)
{
    WRadio *r = data;
    Widget *w = data;

    if (event->type & (GPM_DOWN|GPM_UP)){
	Dlg_head *h = r->widget.parent;

	r->pos = event->y - 1;
	dlg_select_widget (r);
	if (event->type & GPM_UP){
	    radio_callback (w, WIDGET_KEY, ' ');
	    radio_callback (w, WIDGET_FOCUS, 0);
	    h->callback (h, w, DLG_POST_KEY, ' ', NULL);
	    return MOU_NORMAL;
	}
    }
    return MOU_NORMAL;
}

WRadio *
radio_new (int y, int x, int count, const char **texts)
{
    WRadio *result = g_new (WRadio, 1);
    int i, max, m;

    /* Compute the longest string */
    result->texts = g_new (struct hotkey_t, count);

    max = 0;
    for (i = 0; i < count; i++){
        result->texts[i] = parse_hotkey (texts[i]);
        m = hotkey_width (result->texts[i]);
	if (m > max)
	    max = m;
    }

    init_widget (&result->widget, y, x, count, max, radio_callback, radio_event);
    result->state = 1;
    result->pos = 0;
    result->sel = 0;
    result->count = count;
    widget_want_hotkey (result->widget, 1);

    return result;
}


/* Checkbutton widget */

static int check_event (Gpm_Event *event, void *);

static cb_ret_t
check_callback (Widget *w, widget_msg_t msg, int parm)
{
    WCheck *c = (WCheck *) w;
    Dlg_head *h = c->widget.parent;

    switch (msg) {
    case WIDGET_HOTKEY:
        if (c->text.hotkey != NULL) {
            if (g_ascii_tolower ((gchar)c->text.hotkey[0]) ==
                g_ascii_tolower ((gchar)parm)) {

		check_callback (w, WIDGET_KEY, ' ');	/* make action */
		return MSG_HANDLED;
	    }
	}
	return MSG_NOT_HANDLED;

    case WIDGET_KEY:
	if (parm != ' ')
	    return MSG_NOT_HANDLED;
	c->state ^= C_BOOL;
	c->state ^= C_CHANGE;
	h->callback (h, w, DLG_ACTION, 0, NULL);
	check_callback (w, WIDGET_FOCUS, ' ');
	return MSG_HANDLED;

    case WIDGET_CURSOR:
	widget_move (&c->widget, 0, 1);
	return MSG_HANDLED;

    case WIDGET_FOCUS:
    case WIDGET_UNFOCUS:
    case WIDGET_DRAW:
	widget_selectcolor (w, msg == WIDGET_FOCUS, FALSE);
	widget_move (&c->widget, 0, 0);
	tty_print_string ((c->state & C_BOOL) ? "[x] " : "[ ] ");
	draw_hotkey (w, c->text, msg == WIDGET_FOCUS);
	return MSG_HANDLED;

    case WIDGET_DESTROY:
        release_hotkey (c->text);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

static int
check_event (Gpm_Event *event, void *data)
{
    WCheck *c = data;
    Widget *w = data;

    if (event->type & (GPM_DOWN|GPM_UP)){
	Dlg_head *h = c->widget.parent;

	dlg_select_widget (c);
	if (event->type & GPM_UP){
	    check_callback (w, WIDGET_KEY, ' ');
	    check_callback (w, WIDGET_FOCUS, 0);
	    h->callback (h, w, DLG_POST_KEY, ' ', NULL);
	    return MOU_NORMAL;
	}
    }
    return MOU_NORMAL;
}

WCheck *
check_new (int y, int x, int state, const char *text)
{
    WCheck *c =  g_new (WCheck, 1);

    c->text = parse_hotkey (text);

    init_widget (&c->widget, y, x, 1, hotkey_width (c->text),
	check_callback, check_event);
    c->state = state ? C_BOOL : 0;
    widget_want_hotkey (c->widget, 1);

    return c;
}

static gboolean
save_text_to_clip_file (const char *text)
{
    int file;
    char *fname = NULL;

    fname = g_build_filename (home_dir, EDIT_CLIP_FILE, NULL);
    file = mc_open (fname, O_CREAT | O_WRONLY | O_TRUNC,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | O_BINARY);
    g_free (fname);

    if (file == -1)
        return FALSE;

    mc_write (file, (char *) text, strlen (text));
    mc_close (file);
    return TRUE;
}

static gboolean
load_text_from_clip_file (char **text)
{
    char buf[BUF_LARGE];
    FILE *f;
    char *fname = NULL;
    gboolean first = TRUE;

    fname = g_build_filename (home_dir, EDIT_CLIP_FILE, NULL);
    f = fopen (fname, "r");
    g_free (fname);

    if (f == NULL)
        return FALSE;

    *text = NULL;

    while (fgets (buf, sizeof (buf), f)) {
        size_t len;

        len = strlen (buf);
        if ( len > 0 ) {
            if (buf[len - 1] == '\n')
                buf[len - 1] = '\0';

            if (first) {
                first = FALSE;
                *text = g_strdup (buf);
            } else {
                /* remove \n on EOL */
                char *tmp;

                tmp = g_strconcat (*text, " ", buf, (char *) NULL);
                g_free (*text);
                *text = tmp;
            }
        }
    }

    fclose (f);

    return (*text != NULL);
}

static gboolean
panel_save_curent_file_to_clip_file (void)
{
    gboolean res;

    if (current_panel->marked == 0)
        res = save_text_to_clip_file (selection (current_panel)->fname);
    else {
        int i;
        gboolean first = TRUE;
        char *flist = NULL;

        for (i = 0; i < current_panel->count; i++)
            if (current_panel->dir.list[i].f.marked != 0) { /* Skip the unmarked ones */
                if (first) {
                    flist = g_strdup (current_panel->dir.list[i].fname);
                    first = FALSE;
                } else {
                    /* Add empty lines after the file */
                    char *tmp;

                    tmp = g_strconcat (flist, "\n", current_panel->dir.list[i].fname, (char *) NULL);
                    g_free (flist);
                    flist = tmp;
                }
            }

        if (flist != NULL) {
            res = save_text_to_clip_file (flist);
            g_free (flist);
        }
    }
    return res;
}


/* Label widget */

static cb_ret_t
label_callback (Widget *w, widget_msg_t msg, int parm)
{
    WLabel *l = (WLabel *) w;
    Dlg_head *h = l->widget.parent;

    switch (msg) {
    case WIDGET_INIT:
	return MSG_HANDLED;

	/* We don't want to get the focus */
    case WIDGET_FOCUS:
	return MSG_NOT_HANDLED;

    case WIDGET_DRAW:
	{
	    char *p = l->text, *q, c = 0;
	    int y = 0;

	    if (!l->text)
		return MSG_HANDLED;

	    if (l->transparent)
		tty_setcolor (DEFAULT_COLOR);
	    else
		tty_setcolor (DLG_NORMALC (h));

            for (;;) {
		q = strchr (p, '\n');
                if (q != NULL) {
                    c = q[0];
                    q[0] = '\0';
		}

		widget_move (&l->widget, y, 0);
                tty_print_string (str_fit_to_term (p, l->widget.cols, J_LEFT));

                if (q == NULL)
		    break;
                q[0] = c;
		p = q + 1;
		y++;
	    }
	    return MSG_HANDLED;
	}

    case WIDGET_DESTROY:
	g_free (l->text);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

void
label_set_text (WLabel *label, const char *text)
{
    int newcols = label->widget.cols;
    int newlines;

    if (label->text && text && !strcmp (label->text, text))
        return; /* Flickering is not nice */

    g_free (label->text);

    if (text != NULL) {
	label->text = g_strdup (text);
	if (label->auto_adjust_cols) {
            str_msg_term_size (text, &newlines, &newcols);
	    if (newcols > label->widget.cols)
	    label->widget.cols = newcols;
            if (newlines > label->widget.lines)
                label->widget.lines = newlines;
	}
    } else label->text = NULL;

    if (label->widget.parent)
	label_callback ((Widget *) label, WIDGET_DRAW, 0);

    if (newcols < label->widget.cols)
        label->widget.cols = newcols;
}

WLabel *
label_new (int y, int x, const char *text)
{
    WLabel *l;
    int cols = 1;
    int lines = 1;

    if (text != NULL)
        str_msg_term_size (text, &lines, &cols);

    l = g_new (WLabel, 1);
    init_widget (&l->widget, y, x, lines, cols, label_callback, NULL);
    l->text = (text != NULL) ? g_strdup (text) : NULL;
    l->auto_adjust_cols = 1;
    l->transparent = 0;
    widget_want_cursor (l->widget, 0);
    return l;
}


/* Gauge widget (progress indicator) */
/* Currently width is hardcoded here for text mode */
#define gauge_len 47

static cb_ret_t
gauge_callback (Widget *w, widget_msg_t msg, int parm)
{
    WGauge *g = (WGauge *) w;
    Dlg_head *h = g->widget.parent;

    if (msg == WIDGET_INIT)
	return MSG_HANDLED;

    /* We don't want to get the focus */
    if (msg == WIDGET_FOCUS)
	return MSG_NOT_HANDLED;

    if (msg == WIDGET_DRAW){
	widget_move (&g->widget, 0, 0);
	tty_setcolor (DLG_NORMALC (h));
	if (!g->shown)
	    tty_printf ("%*s", gauge_len, "");
	else {
	    int percentage, columns;
	    long total = g->max, done = g->current;

	    if (total <= 0 || done < 0) {
	        done = 0;
	        total = 100;
	    }
	    if (done > total)
	        done = total;
	    while (total > 65535) {
	        total /= 256;
	        done /= 256;
	    }
	    percentage = (200 * done / total + 1) / 2;
	    columns = (2 * (gauge_len - 7) * done / total + 1) / 2;
	    tty_print_char ('[');
	    tty_setcolor (GAUGE_COLOR);
	    tty_printf ("%*s", (int) columns, "");
	    tty_setcolor (DLG_NORMALC (h));
	    tty_printf ("%*s] %3d%%", (int)(gauge_len - 7 - columns), "", (int) percentage);
	}
	return MSG_HANDLED;
    }

    return default_proc (msg, parm);
}

void
gauge_set_value (WGauge *g, int max, int current)
{
    if (g->current == current && g->max == max)
    	return; /* Do not flicker */
    if (max == 0)
        max = 1; /* I do not like division by zero :) */

    g->current = current;
    g->max = max;
    gauge_callback ((Widget *) g, WIDGET_DRAW, 0);
}

void
gauge_show (WGauge *g, int shown)
{
    if (g->shown == shown)
        return;
    g->shown = shown;
    gauge_callback ((Widget *) g, WIDGET_DRAW, 0);
}

WGauge *
gauge_new (int y, int x, int shown, int max, int current)
{
    WGauge *g = g_new (WGauge, 1);

    init_widget (&g->widget, y, x, 1, gauge_len, gauge_callback, NULL);
    g->shown = shown;
    if (max == 0)
        max = 1; /* I do not like division by zero :) */
    g->max = max;
    g->current = current;
    widget_want_cursor (g->widget, 0);
    return g;
}


/* Input widget */

/* {{{ history button */

#define LARGE_HISTORY_BUTTON 1

#ifdef LARGE_HISTORY_BUTTON
#  define HISTORY_BUTTON_WIDTH 3
#else
#  define HISTORY_BUTTON_WIDTH 1
#endif

#define should_show_history_button(in) \
(in->history && in->field_width > HISTORY_BUTTON_WIDTH * 2 + 1 && in->widget.parent)

static void draw_history_button (WInput * in)
{
    char c;
    c = in->history->next ? (in->history->prev ? '|' : 'v') : '^';
    widget_move (&in->widget, 0, in->field_width - HISTORY_BUTTON_WIDTH);
#ifdef LARGE_HISTORY_BUTTON
    {
	Dlg_head *h;
	h = in->widget.parent;
	tty_setcolor (NORMAL_COLOR);
	tty_print_string ("[ ]");
	/* Too distracting: tty_setcolor (MARKED_COLOR); */
        widget_move (&in->widget, 0, in->field_width - HISTORY_BUTTON_WIDTH + 1);
	tty_print_char (c);
    }
#else
    tty_setcolor (MARKED_COLOR);
    tty_print_char (c);
#endif
}

/* }}} history button */


/* Input widgets now have a global kill ring */
/* Pointer to killed data */
static char *kill_buffer = 0;

void
update_input (WInput *in, int clear_first)
{
    int has_history = 0;
    int    i;
    int    buf_len = str_length (in->buffer);
    const char *cp;
    int pw;

    if (should_show_history_button (in))
	has_history = HISTORY_BUTTON_WIDTH;

    if (in->disable_update)
	return;

    pw = str_term_width2 (in->buffer, in->point);

    /* Make the point visible */
    if ((pw < in->term_first_shown) ||
         (pw >= in->term_first_shown + in->field_width - has_history)) {

        in->term_first_shown = pw - (in->field_width / 3);
        if (in->term_first_shown < 0)
            in->term_first_shown = 0;
    }

    /* Adjust the mark */
    if (in->mark > buf_len)
	in->mark = buf_len;

    if (has_history)
	draw_history_button (in);

    tty_setcolor (in->color);

    widget_move (&in->widget, 0, 0);

    if (!in->is_password) {
        tty_print_string (str_term_substring (in->buffer, in->term_first_shown,
                in->field_width - has_history));
    } else {
        cp = in->buffer;
        for (i = -in->term_first_shown; i < in->field_width - has_history; i++){
            if (i >= 0) {
                tty_print_char ((cp[0] != '\0') ? '*' : ' ');
            }
            if (cp[0] != '\0') str_cnext_char (&cp);
        }
    }

    if (clear_first)
	    in->first = 0;
}

void
winput_set_origin (WInput *in, int x, int field_width)
{
    in->widget.x    = x;
    in->field_width = in->widget.cols = field_width;
    update_input (in, 0);
}

/* {{{ history saving and loading */

int num_history_items_recorded = 60;

/*
   This loads and saves the history of an input line to and from the
   widget. It is called with the widgets history name on creation of the
   widget, and returns the GList list. It stores histories in the file
   ~/.mc/history in using the profile code.

   If def_text is passed as INPUT_LAST_TEXT (to the input_new()
   function) then input_new assigns the default text to be the last text
   entered, or "" if not found.
 */

GList *
history_get (const char *input_name)
{
    size_t i;
    GList *hist = NULL;
    char *profile;
    mc_config_t *cfg;
    char **keys;
    size_t keys_num = 0;
    char *this_entry;

    if (num_history_items_recorded == 0)	/* this is how to disable */
	return NULL;
    if ((input_name == NULL) || (*input_name == '\0'))
	return NULL;

    profile = g_build_filename (home_dir, MC_USERCONF_DIR, MC_HISTORY_FILE, NULL);
    cfg = mc_config_init (profile);

    /* get number of keys */
    keys = mc_config_get_keys (cfg, input_name, &keys_num);
    g_strfreev (keys);

    for (i = 0; i < keys_num; i++) {
	char key[BUF_TINY];
	g_snprintf (key, sizeof (key), "%lu", (unsigned long)i);
	this_entry = mc_config_get_string (cfg, input_name, key, "");

	if (this_entry != NULL)
	    hist = list_append_unique (hist, this_entry);
    }

    mc_config_deinit (cfg);
    g_free (profile);

    /* return pointer to the last entry in the list */
    return g_list_last (hist);
}

void
history_put (const char *input_name, GList *h)
{
    int i;
    char *profile;
    mc_config_t *cfg;

    if (num_history_items_recorded == 0)	/* this is how to disable */
	return;
    if ((input_name == NULL) || (*input_name == '\0'))
	return;
    if (h == NULL)
	return;

    profile = g_build_filename (home_dir, MC_USERCONF_DIR, MC_HISTORY_FILE, NULL);

    i = open (profile, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (i != -1)
	close (i);

    /* Make sure the history is only readable by the user */
    if (chmod (profile, S_IRUSR | S_IWUSR) == -1 && errno != ENOENT) {
	g_free (profile);
	return;
    }

    /* go to end of list */
    h = g_list_last (h);

    /* go back 60 places */
    for (i = 0; (i < num_history_items_recorded - 1) && (h->prev != NULL); i++)
	h = g_list_previous (h);

    cfg = mc_config_init (profile);

    if (input_name != NULL)
	mc_config_del_group (cfg, input_name);

    /* dump history into profile */
    for (i = 0; h != NULL; h = g_list_next (h)) {
	char *text = (char *) h->data;

	/* We shouldn't have null entries, but let's be sure */
	if (text != NULL) {
	    char key[BUF_TINY];
	    g_snprintf (key, sizeof (key), "%d", i++);
	    mc_config_set_string (cfg, input_name, key, text);
	}
    }

    mc_config_save_file (cfg, NULL);
    mc_config_deinit(cfg);
    g_free (profile);
}

/* }}} history saving and loading */


/* {{{ history display */

static const char *
i18n_htitle (void)
{
    return _(" History ");
}

typedef struct {
    Widget *widget;
    size_t count;
    size_t maxlen;
} dlg_hist_data;

static cb_ret_t
dlg_hist_reposition (Dlg_head *dlg_head)
{
    dlg_hist_data *data;
    int x = 0, y, he, wi;

    /* guard checks */
    if ((dlg_head == NULL) || (dlg_head->data == NULL))
	return MSG_NOT_HANDLED;

    data = (dlg_hist_data *) dlg_head->data;

    y = data->widget->y;
    he = data->count + 2;

    if (he <= y || y > (LINES - 6)) {
        he = min (he, y - 1);
        y -= he;
    } else {
        y++;
        he = min (he, LINES - y);
    }

    if (data->widget->x > 2)
        x = data->widget->x - 2;

    wi = data->maxlen + 4;

    if ((wi + x) > COLS) {
        wi = min (wi, COLS);
        x = COLS - wi;
    }

    dlg_set_position (dlg_head, y, x, y + he, x + wi);

    return MSG_HANDLED;
}

static cb_ret_t
dlg_hist_callback (Dlg_head *h, Widget *sender,
		    dlg_msg_t msg, int parm, void *data)
{
    switch (msg) {
    case DLG_RESIZE:
	return dlg_hist_reposition (h);

    default:
	return default_dlg_callback (h, sender, msg, parm, data);
    }
}

char *
show_hist (GList **history, Widget *widget)
{
    GList *z, *hlist = NULL, *hi;
    size_t maxlen, i, count = 0;
    char *r = NULL;
    Dlg_head *query_dlg;
    WListbox *query_list;
    dlg_hist_data hist_data;

    if (*history == NULL)
	return NULL;

    maxlen = str_term_width1 (i18n_htitle ()) + 2;

    for (z = *history; z != NULL; z = g_list_previous (z)) {
	WLEntry *entry;

	i = str_term_width1 ((char *) z->data);
	maxlen = max (maxlen, i);
	count++;

	entry = g_new0 (WLEntry, 1);
	/* history is being reverted here */
	entry->text = g_strdup ((char *) z->data);
	hlist = g_list_prepend (hlist, entry);
    }

    hist_data.widget = widget;
    hist_data.count = count;
    hist_data.maxlen = maxlen;

    query_dlg =
	create_dlg (0, 0, 4, 4, dialog_colors, dlg_hist_callback,
			"[History-query]", i18n_htitle (), DLG_COMPACT);
    query_dlg->data = &hist_data;

    query_list = listbox_new (1, 1, 2, 2, TRUE, NULL);

    /* this call makes list stick to all sides of dialog, effectively make
       it be resized with dialog */
    add_widget_autopos (query_dlg, query_list, WPOS_KEEP_ALL);

    /* to avoid diplicating of (calculating sizes in two places)
       code, call dlg_hist_callback function here, to set dialog and
       controls positions.
       The main idea - create 4x4 dialog and add 2x2 list in
       center of it, and let dialog function resize it to needed
       size. */
    dlg_hist_callback (query_dlg, NULL, DLG_RESIZE, 0, NULL);

    if (query_dlg->y < widget->y) {
	/* draw list entries from bottom upto top */
	listbox_set_list (query_list, hlist);
	listbox_select_last (query_list);
    } else {
	/* draw list entries from top downto bottom */
	/* revert history direction */
	hlist = g_list_reverse (hlist);
	listbox_set_list (query_list, hlist);
    }

    if (run_dlg (query_dlg) != B_CANCEL) {
	char *q;

	listbox_get_current (query_list, &q, NULL);
	if (q != NULL)
	    r = g_strdup (q);
    }

    /* get modified history from dialog */
    z = NULL;
    for (hi = query_list->list; hi != NULL; hi = g_list_next (hi)) {
	WLEntry *entry;

	entry = (WLEntry *) hi->data;
	/* history is being reverted here again */
	z = g_list_prepend (z, entry->text);
	entry->text = NULL;
    }

    destroy_dlg (query_dlg);

    /* restore history direction */
    if (query_dlg->y < widget->y)
	z = g_list_reverse (z);

    g_list_foreach (*history, (GFunc) g_free, NULL);
    g_list_free (*history);
    *history = g_list_last (z);

    return r;
}

static void
do_show_hist (WInput *in)
{
    char *r;

    r = show_hist (&in->history, &in->widget);
    if (r != NULL) {
	assign_text (in, r);
	g_free (r);
    }
}

/* }}} history display */

static void
input_destroy (WInput *in)
{
    if (!in){
	fprintf (stderr, "Internal error: null Input *\n");
	exit (1);
    }

    new_input (in);

    if (in->history){
	if (!in->is_password)	/* don't save passwords ;-) */
	    history_put (in->history_name, in->history);

	in->history = g_list_first (in->history);
	g_list_foreach (in->history, (GFunc) g_free, NULL);
	g_list_free (in->history);
    }

    g_free (in->buffer);
    free_completions (in);
    g_free (in->history_name);
}

void
input_disable_update (WInput *in)
{
    in->disable_update++;
}

void
input_enable_update (WInput *in)
{
    in->disable_update--;
    update_input (in, 0);
}


static void
push_history (WInput *in, const char *text)
{
    /* input widget where urls with passwords are entered without any
       vfs prefix */
    const char *password_input_fields[] = {
	N_(" Link to a remote machine "),
	N_(" FTP to machine "),
	N_(" SMB link to machine ")
    };
    const size_t ELEMENTS = (sizeof (password_input_fields) /
				sizeof (password_input_fields[0]));

    char *t;
    size_t i;
    gboolean empty;

    if (text == NULL)
	return;

#ifdef ENABLE_NLS
    for (i = 0; i < ELEMENTS; i++)
	password_input_fields[i] = _(password_input_fields[i]);
#endif

    t = g_strstrip (g_strdup (text));
    empty = *t == '\0';
    g_free (t);
    t = g_strdup (empty ? "" : text);

    if (in->history_name != NULL) {
	const char *p = in->history_name + 3;

	for (i = 0; i < ELEMENTS; i++)
	    if (strcmp (p, password_input_fields[i]) == 0)
		break;

	strip_password (t, i >= ELEMENTS);
    }

    in->history = list_append_unique (in->history, t);
    in->need_push = 0;
}

/* Cleans the input line and adds the current text to the history */
void
new_input (WInput *in)
{
    push_history (in, in->buffer);
    in->need_push = 1;
    in->buffer[0] = '\0';
    in->point = 0;
    in->charpoint = 0;
    in->mark = 0;
    free_completions (in);
    update_input (in, 0);
}

static void
move_buffer_backward (WInput *in, int start, int end)
{
    int i, pos, len;
    int str_len = str_length (in->buffer);
    if (start >= str_len || end > str_len + 1) return;

    pos = str_offset_to_pos (in->buffer, start);
    len = str_offset_to_pos (in->buffer, end) - pos;

    for (i = pos; in->buffer[i + len - 1]; i++)
        in->buffer[i] = in->buffer[i + len];
}

static cb_ret_t
insert_char (WInput *in, int c_code)
{
    size_t i;
    int res;

    if (c_code == -1)
	return MSG_NOT_HANDLED;

    if (in->charpoint >= MB_LEN_MAX)
	return MSG_HANDLED;

    in->charbuf[in->charpoint] = c_code;
    in->charpoint++;

    res = str_is_valid_char (in->charbuf, in->charpoint);
    if (res < 0) {
        if (res != -2)
	    in->charpoint = 0; /* broken multibyte char, skip */
        return MSG_HANDLED;
    }

    in->need_push = 1;
    if (strlen (in->buffer) + 1 + in->charpoint >= in->current_max_size){
	/* Expand the buffer */
        size_t new_length = in->current_max_size +
                in->field_width + in->charpoint;
        char *narea = g_try_renew (char, in->buffer, new_length);
	if (narea){
	    in->buffer = narea;
            in->current_max_size = new_length;
	}
    }

    if (strlen (in->buffer) + in->charpoint < in->current_max_size) {
        /* bytes from begin */
        size_t ins_point = str_offset_to_pos (in->buffer, in->point);
        /* move chars */
        size_t rest_bytes = strlen (in->buffer + ins_point);

        for (i = rest_bytes + 1; i > 0; i--)
            in->buffer[ins_point + i + in->charpoint - 1] =
                    in->buffer[ins_point + i - 1];

        memcpy(in->buffer + ins_point, in->charbuf, in->charpoint);
	in->point++;
    }

    in->charpoint = 0;
    return MSG_HANDLED;
}

static void
beginning_of_line (WInput *in)
{
    in->point = 0;
    in->charpoint = 0;
}

static void
end_of_line (WInput *in)
{
    in->point = str_length (in->buffer);
    in->charpoint = 0;
}

static void
backward_char (WInput *in)
{
    const char *act = in->buffer + str_offset_to_pos (in->buffer, in->point);

    if (in->point > 0) {
        in->point-= str_cprev_noncomb_char (&act, in->buffer);
    }
    in->charpoint = 0;
}

static void
forward_char (WInput *in)
{
    const char *act = in->buffer + str_offset_to_pos (in->buffer, in->point);
    if (act[0] != '\0') {
	in->point+= str_cnext_noncomb_char (&act);
    }
    in->charpoint = 0;
}

static void
forward_word (WInput * in)
{
    const char *p = in->buffer + str_offset_to_pos (in->buffer, in->point);

    while (p[0] != '\0' && (str_isspace (p) || str_ispunct (p))) {
        str_cnext_char (&p);
        in->point++;
    }
    while (p[0] != '\0' && !str_isspace (p) && !str_ispunct (p)) {
        str_cnext_char (&p);
        in->point++;
    }
}

static void
backward_word (WInput *in)
{
    const char *p;
    const char *p_tmp;

    for (
        p = in->buffer + str_offset_to_pos (in->buffer, in->point);
        (p != in->buffer) && (p[0] == '\0');
        str_cprev_char (&p), in->point--
    );

    while (p != in->buffer) {
        p_tmp = p;
        str_cprev_char (&p);
        if (!str_isspace (p) && !str_ispunct (p)) {
            p = p_tmp;
            break;
        }
        in->point--;
    }
    while (p != in->buffer) {
        str_cprev_char (&p);
        if (str_isspace (p) || str_ispunct (p))
            break;

        in->point--;
    }
}

static void
key_left (WInput *in)
{
    backward_char (in);
}

static void
key_ctrl_left (WInput *in)
{
    backward_word (in);
}

static void
key_right (WInput *in)
{
    forward_char (in);
}

static void
key_ctrl_right (WInput *in)
{
    forward_word (in);
}
static void
backward_delete (WInput *in)
{
    const char *act = in->buffer + str_offset_to_pos (in->buffer, in->point);
    int start;

    if (in->point == 0)
	return;

    start = in->point - str_cprev_noncomb_char (&act, in->buffer);
    move_buffer_backward(in, start, in->point);
    in->charpoint = 0;
    in->need_push = 1;
    in->point = start;
}

static void
delete_char (WInput *in)
{
    const char *act = in->buffer + str_offset_to_pos (in->buffer, in->point);
    int end = in->point;

    end+= str_cnext_noncomb_char (&act);

    move_buffer_backward(in, in->point, end);
    in->charpoint = 0;
    in->need_push = 1;
}

static void
copy_region (WInput *in, int x_first, int x_last)
{
    int first = min (x_first, x_last);
    int last  = max (x_first, x_last);

    if (last == first) {
        /* Copy selected files to clipboard */
        panel_save_curent_file_to_clip_file ();
        return;
    }

    g_free (kill_buffer);

    first = str_offset_to_pos (in->buffer, first);
    last = str_offset_to_pos (in->buffer, last);

    kill_buffer = g_strndup(in->buffer + first, last - first);
    save_text_to_clip_file (kill_buffer);
}

static void
delete_region (WInput *in, int x_first, int x_last)
{
    int first = min (x_first, x_last);
    int last  = max (x_first, x_last);
    size_t len;

    in->point = first;
    if (in->mark > first)
        in->mark  = first;
    last = str_offset_to_pos (in->buffer, last);
    first = str_offset_to_pos (in->buffer, first);
    len = strlen (&in->buffer[last]) + 1;
    memmove (&in->buffer[first], &in->buffer[last], len);
    in->charpoint = 0;
    in->need_push = 1;
}

static void
kill_word (WInput *in)
{
    int old_point = in->point;
    int new_point;

    forward_word (in);
    new_point = in->point;
    in->point = old_point;

    copy_region (in, old_point, new_point);
    delete_region (in, old_point, new_point);
    in->need_push = 1;
    in->charpoint = 0;
    in->charpoint = 0;
}

static void
back_kill_word (WInput *in)
{
    int old_point = in->point;
    int new_point;

    backward_word (in);
    new_point = in->point;
    in->point = old_point;

    copy_region (in, old_point, new_point);
    delete_region (in, old_point, new_point);
    in->need_push = 1;
}

static void
set_mark (WInput *in)
{
    in->mark = in->point;
}

static void
kill_save (WInput *in)
{
    copy_region (in, in->mark, in->point);
}

static void
kill_region (WInput *in)
{
    kill_save (in);
    delete_region (in, in->point, in->mark);
}

static void
clear_region (WInput *in)
{
    delete_region (in, in->point, in->mark);
}

static void
yank (WInput *in)
{
    char *p;

    if (!kill_buffer)
        return;
    in->charpoint = 0;
    for (p = kill_buffer; *p; p++)
	insert_char (in, *p);
    in->charpoint = 0;
}

static void
kill_line (WInput *in)
{
    int chp = str_offset_to_pos (in->buffer, in->point);
    g_free (kill_buffer);
    kill_buffer = g_strdup (&in->buffer[chp]);
    in->buffer[chp] = '\0';
    in->charpoint = 0;
}

static void
ins_from_clip (WInput *in)
{
    char *p = NULL;

    if (load_text_from_clip_file (&p)) {
        char *pp;

        for (pp = p; *pp != '\0'; pp++)
            insert_char (in, *pp);

        g_free (p);
    }
}

void
assign_text (WInput *in, const char *text)
{
    free_completions (in);
    g_free (in->buffer);
    in->buffer = g_strdup (text);	/* was in->buffer->text */
    in->current_max_size = strlen (in->buffer) + 1;
    in->point = str_length (in->buffer);
    in->mark = 0;
    in->need_push = 1;
    in->charpoint = 0;
}

static void
hist_prev (WInput *in)
{
    GList *prev;

    if (!in->history)
	return;

    if (in->need_push)
	push_history (in, in->buffer);

    prev = g_list_previous (in->history);
    if (prev != NULL) {
	in->history = prev;
	assign_text (in, (char *) prev->data);
	in->need_push = 0;
    }
}

static void
hist_next (WInput *in)
{
    if (in->need_push) {
        push_history (in, in->buffer);
        assign_text (in, "");
        return;
    }

    if (!in->history)
	return;

    if (!in->history->next) {
        assign_text (in, "");
	return;
    }

    in->history = g_list_next (in->history);
    assign_text (in, (char *) in->history->data);
    in->need_push = 0;
}

static void
port_region_marked_for_delete (WInput *in)
{
    in->buffer[0] = '\0';
    in->point = 0;
    in->first = 0;
    in->charpoint = 0;
}

static cb_ret_t
input_execute_cmd (WInput *in, unsigned long command)
{
    cb_ret_t res = MSG_HANDLED;

    switch (command) {
    case CK_InputBol:
        beginning_of_line (in);
        break;
    case CK_InputEol:
        end_of_line (in);
        break;
    case CK_InputMoveLeft:
        key_left (in);
        break;
    case CK_InputWordLeft:
        key_ctrl_left (in);
        break;
    case CK_InputMoveRight:
        key_right (in);
        break;
    case CK_InputWordRight:
        key_ctrl_right (in);
        break;
    case CK_InputBackwardChar:
        backward_char (in);
        break;
    case CK_InputBackwardWord:
        backward_word (in);
        break;
    case CK_InputForwardChar:
        forward_char (in);
        break;
    case CK_InputForwardWord:
        forward_word (in);
        break;
    case CK_InputBackwardDelete:
        backward_delete (in);
        break;
    case CK_InputDeleteChar:
        delete_char (in);
        break;
    case CK_InputKillWord:
        kill_word (in);
        break;
    case CK_InputBackwardKillWord:
        back_kill_word (in);
        break;
    case CK_InputSetMark:
        set_mark (in);
        break;
    case CK_InputKillRegion:
        kill_region (in);
        break;
    case CK_InputClearLine:
        clear_region (in);
        break;
    case CK_InputKillSave:
        kill_save (in);
        break;
    case CK_InputYank:
        yank (in);
        break;
    case CK_InputPaste:
        ins_from_clip (in);
        break;
    case CK_InputKillLine:
        kill_line (in);
        break;
    case CK_InputHistoryPrev:
        hist_prev (in);
        break;
    case CK_InputHistoryNext:
        hist_next (in);
        break;
    case CK_InputHistoryShow:
        do_show_hist (in);
        break;
    case CK_InputComplete:
        complete (in);
        break;
    default:
        res = MSG_NOT_HANDLED;
    }

    return res;
}

/* This function is a test for a special input key used in complete.c */
/* Returns 0 if it is not a special key, 1 if it is a non-complete key
   and 2 if it is a complete key */
int
is_in_input_map (WInput *in, int key)
{
    size_t i;
    for (i = 0; input_map[i].key != 0; i++)
        if (key == input_map[i].key) {
            input_execute_cmd (in, input_map[i].command);
            return (input_map[i].command == CK_InputComplete) ? 2 : 1;
        }
    return 0;
}

cb_ret_t
handle_char (WInput *in, int key)
{
    cb_ret_t v;
    int    i;

    v = MSG_NOT_HANDLED;

    if (quote) {
        free_completions (in);
        v = insert_char (in, key);
        update_input (in, 1);
        quote = 0;
        return v;
    }
    for (i = 0; input_map[i].key; i++) {
        if (key == input_map[i].key) {
            if (input_map[i].command != CK_InputComplete)
                free_completions (in);
            input_execute_cmd (in, input_map[i].command);
            update_input (in, 1);
            v = MSG_HANDLED;
            break;
        }
    }
    if (input_map[i].command == 0) {
        if (key > 255)
            return MSG_NOT_HANDLED;
        if (in->first)
            port_region_marked_for_delete (in);
        free_completions (in);
        v = insert_char (in, key);
    }
    update_input (in, 1);
    return v;
}

/* Inserts text in input line */
void
stuff (WInput *in, const char *text, int insert_extra_space)
{
    input_disable_update (in);
    while (*text != '\0')
	handle_char (in, (unsigned char) *text++); /* unsigned extension char->int */
    if (insert_extra_space)
	handle_char (in, ' ');
    input_enable_update (in);
    update_input (in, 1);
}

void
input_set_point (WInput *in, int pos)
{
    int max_pos = str_length (in->buffer);

    if (pos > max_pos)
        pos = max_pos;
    if (pos != in->point)
    	free_completions (in);
    in->point = pos;
    in->charpoint = 0;
    update_input (in, 1);
}

cb_ret_t
input_callback (Widget *w, widget_msg_t msg, int parm)
{
    WInput *in = (WInput *) w;
    cb_ret_t v;

    switch (msg) {
    case WIDGET_KEY:
	if (parm == XCTRL ('q')) {
	    quote = 1;
	    v = handle_char (in, ascii_alpha_to_cntrl (tty_getch ()));
	    quote = 0;
	    return v;
	}

	/* Keys we want others to handle */
	if (parm == KEY_UP || parm == KEY_DOWN || parm == ESC_CHAR
	    || parm == KEY_F (10) || parm == '\n')
	    return MSG_NOT_HANDLED;

	/* When pasting multiline text, insert literal Enter */
	if ((parm & ~KEY_M_MASK) == '\n') {
	    quote = 1;
	    v = handle_char (in, '\n');
	    quote = 0;
	    return v;
	}

	return handle_char (in, parm);

    case WIDGET_COMMAND:
	return input_execute_cmd (in, parm);

    case WIDGET_FOCUS:
    case WIDGET_UNFOCUS:
    case WIDGET_DRAW:
	update_input (in, 0);
	return MSG_HANDLED;

    case WIDGET_CURSOR:
            widget_move (&in->widget, 0, str_term_width2 (in->buffer, in->point)
                    - in->term_first_shown);
	return MSG_HANDLED;

    case WIDGET_DESTROY:
	input_destroy (in);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

static int
input_event (Gpm_Event * event, void *data)
{
    WInput *in = data;

    if (event->type & (GPM_DOWN | GPM_DRAG)) {
	dlg_select_widget (in);

        if (event->x >= in->field_width - HISTORY_BUTTON_WIDTH + 1
	    && should_show_history_button (in)) {
	    do_show_hist (in);
	} else {
            in->point = str_length (in->buffer);
            if (event->x + in->term_first_shown - 1 < str_term_width1 (in->buffer))
                in->point = str_column_to_pos (in->buffer, event->x
                           + in->term_first_shown - 1);
	}
	update_input (in, 1);
    }
    return MOU_NORMAL;
}

WInput *
input_new (int y, int x, int color, int width, const char *def_text,
	   const char *histname, INPUT_COMPLETE_FLAGS completion_flags)
{
    WInput *in = g_new (WInput, 1);
    size_t initial_buffer_len;

    init_widget (&in->widget, y, x, 1, width, input_callback, input_event);

    /* history setup */
    in->history_name = NULL;
    in->history = NULL;
    if ((histname != NULL) && (*histname != '\0')) {
	in->history_name = g_strdup (histname);
	in->history = history_get (histname);
    }

    if (def_text == NULL)
	def_text = "";
    else if (def_text == INPUT_LAST_TEXT) {
	if ((in->history != NULL) && (in->history->data != NULL))
	    def_text = (char *) in->history->data;
	else
	    def_text = "";
    }

    initial_buffer_len = 1 + max ((size_t) width, strlen (def_text));
    in->widget.options |= W_IS_INPUT;
    in->completions = NULL;
    in->completion_flags = completion_flags;
    in->current_max_size = initial_buffer_len;
    in->buffer = g_new (char, initial_buffer_len);
    in->color = color;
    in->field_width = width;
    in->first = 1;
    in->term_first_shown = 0;
    in->disable_update = 0;
    in->mark = 0;
    in->need_push = 1;
    in->is_password = 0;

    strcpy (in->buffer, def_text);
    in->point = str_length (in->buffer);
    in->charpoint = 0;

    return in;
}


/* Listbox widget */

/* Should draw the scrollbar, but currently draws only
 * indications that there is more information
 */

static void
listbox_entry_free (void *data)
{
    WLEntry *e = data;
    g_free (e->text);
    g_free (e);
}

static void
listbox_drawscroll (WListbox *l)
{
    const int max_line = l->widget.lines - 1;
    int line = 0;
    int i;

    /* Are we at the top? */
    widget_move (&l->widget, 0, l->widget.cols);
    if (l->top == 0)
	tty_print_one_vline ();
    else
	tty_print_char ('^');

    /* Are we at the bottom? */
    widget_move (&l->widget, max_line, l->widget.cols);
    if ((l->top + l->widget.lines == l->count) || (l->widget.lines >= l->count))
	tty_print_one_vline ();
    else
	tty_print_char ('v');

    /* Now draw the nice relative pointer */
    if (l->count != 0)
	line = 1 + ((l->pos * (l->widget.lines - 2)) / l->count);

    for (i = 1; i < max_line; i++) {
	widget_move (&l->widget, i, l->widget.cols);
	if (i != line)
	    tty_print_one_vline ();
	else
	    tty_print_char ('*');
    }
}

static void
listbox_draw (WListbox *l, gboolean focused)
{
    const Dlg_head *h = l->widget.parent;
    const int normalc = DLG_NORMALC (h);
    int selc = focused ? DLG_HOT_FOCUSC (h) : DLG_FOCUSC (h);

    GList *le;
    int pos;
    int i;
    int sel_line = -1;

    le = g_list_nth (l->list, l->top);
/*    pos = (le == NULL) ? 0 : g_list_position (l->list, le); */
    pos = (le == NULL) ? 0 : l->top;

    for (i = 0; i < l->widget.lines; i++) {
	const char *text;

	/* Display the entry */
	if (pos == l->pos && sel_line == -1) {
	    sel_line = i;
	    tty_setcolor (selc);
	} else
	    tty_setcolor (normalc);

	widget_move (&l->widget, i, 1);

	if ((i > 0 && pos >= l->count) || (l->list == NULL) || (le == NULL))
	    text = "";
	else {
	    WLEntry *e = (WLEntry *) le->data;
	    text = e->text;
	    le = g_list_next (le);
	    pos++;
	}

	tty_print_string (str_fit_to_term (text, l->widget.cols - 2, J_LEFT_FIT));
    }

    l->cursor_y = sel_line;

    if (l->scrollbar && (l->count > l->widget.lines)) {
	tty_setcolor (normalc);
	listbox_drawscroll (l);
    }
}

static int
listbox_check_hotkey (WListbox *l, int key)
{
    int i;
    GList *le;

    for (i = 0, le = l->list; le != NULL; i++, le = g_list_next (le)) {
	WLEntry *e = (WLEntry *) le->data;

	if (e->hotkey == key)
	    return i;
    }

    return (-1);
}

/* Selects the last entry and scrolls the list to the bottom */
void
listbox_select_last (WListbox *l)
{
    l->pos = l->count - 1;
    l->top = (l->count > l->widget.lines) ? (l->count - l->widget.lines) : 0;
}

/* Selects the first entry and scrolls the list to the top */
void
listbox_select_first (WListbox *l)
{
    l->pos = l->top = 0;
}

void
listbox_set_list (WListbox *l, GList *list)
{
    listbox_remove_list (l);

    if (l != NULL) {
	l->list = list;
	l->top = l->pos = 0;
	l->count = g_list_length (list);
    }
}

void
listbox_remove_list (WListbox *l)
{
    if ((l != NULL) && (l->count != 0)) {
	g_list_foreach (l->list, (GFunc) listbox_entry_free, NULL);
	g_list_free (l->list);
	l->list = NULL;
	l->count = l->pos = l->top = 0;
    }
}

void
listbox_remove_current (WListbox *l)
{
    if ((l != NULL) && (l->count != 0)) {
	GList *current;

	current = g_list_nth (l->list, l->pos);
	l->list = g_list_remove_link (l->list, current);
	listbox_entry_free ((WLEntry *) current->data);
	g_list_free_1 (current);
	l->count--;

	if (l->count == 0)
	    l->top = l->pos = 0;
	else if (l->pos >= l->count)
	    l->pos = l->count - 1;
    }
}

void
listbox_select_entry (WListbox *l, int dest)
{
    GList *le;
    int pos;
    gboolean top_seen = FALSE;

    if (dest < 0)
	return;

    /* Special case */
    for (pos = 0, le = l->list; le != NULL; pos++, le = g_list_next (le)) {
	if (pos == l->top)
	    top_seen = TRUE;

	if (pos == dest) {
	    l->pos = dest;
	    if (!top_seen)
		l->top = l->pos;
	    else
		if (l->pos - l->top >= l->widget.lines)
		    l->top = l->pos - l->widget.lines + 1;
	    return;
	}
    }

    /* If we are unable to find it, set decent values */
    l->pos = l->top = 0;
}

/* Selects from base the pos element */
static int
listbox_select_pos (WListbox *l, int base, int pos)
{
    int last = l->count - 1;

    base += pos;
    if (base >= last)
	base = last;

    return base;
}

static void
listbox_fwd (WListbox *l)
{
    if (l->pos + 1 >= l->count)
        listbox_select_first (l);
    else
        listbox_select_entry (l, l->pos + 1);
}

static void
listbox_back (WListbox *l)
{
    if (l->pos <= 0)
        listbox_select_last (l);
    else
        listbox_select_entry (l, l->pos - 1);
}

/* Return MSG_HANDLED if we want a redraw */
static cb_ret_t
listbox_key (WListbox *l, int key)
{
    int i;

    cb_ret_t j = MSG_NOT_HANDLED;

    if (l->list == NULL)
	return MSG_NOT_HANDLED;

    /* focus on listbox item N by '0'..'9' keys */
    if (key >= '0' && key <= '9') {
	int oldpos = l->pos;
	listbox_select_entry (l, key - '0');

	/* need scroll to item? */
	if (abs (oldpos - l->pos) > l->widget.lines)
	    l->top = l->pos;

	return MSG_HANDLED;
    }

    switch (key){
    case KEY_HOME:
    case KEY_A1:
    case ALT ('<'):
        listbox_select_first (l);
	return MSG_HANDLED;

    case KEY_END:
    case KEY_C1:
    case ALT ('>'):
        listbox_select_last (l);
	return MSG_HANDLED;

    case XCTRL('p'):
    case KEY_UP:
	listbox_back (l);
	return MSG_HANDLED;

    case XCTRL('n'):
    case KEY_DOWN:
	listbox_fwd (l);
	return MSG_HANDLED;

    case KEY_NPAGE:
    case XCTRL('v'):
	for (i = 0; (i < l->widget.lines - 1) && (l->pos < l->count - 1); i++) {
	    listbox_fwd (l);
	    j = MSG_HANDLED;
	}
	break;

    case KEY_PPAGE:
    case ALT('v'):
	for (i = 0; (i < l->widget.lines - 1) && (l->pos > 0); i++) {
	    listbox_back (l);
	    j = MSG_HANDLED;
	}
	break;

    case KEY_DC:
    case 'd':
	if (l->deletable) {
	    gboolean is_last = (l->pos + 1 >= l->count);
	    gboolean is_more = (l->top + l->widget.lines >= l->count);

	    listbox_remove_current (l);
	    if ((l->top > 0) && (is_last || is_more))
		l->top--;
	}
	return MSG_HANDLED;

    case (KEY_M_SHIFT | KEY_DC):
    case 'D':
	if (l->deletable && confirm_history_cleanup
	    /* TRANSLATORS: no need to translate 'DialogTitle', it's just a context prefix */
	    && (query_dialog (Q_("DialogTitle|History cleanup"),
			_("Do you want clean this history?"),
			D_ERROR, 2, _("&Yes"), _("&No")) == 0)) {
	    listbox_remove_list (l);
	    j = MSG_HANDLED;
	}
	break;

    default:
	break;
    }

    return j;
}

static inline void
listbox_destroy (WListbox *l)
{
    /* don't delete list in modifable listbox */
    if (!l->deletable)
	listbox_remove_list (l);
}

static cb_ret_t
listbox_callback (Widget *w, widget_msg_t msg, int parm)
{
    WListbox *l = (WListbox *) w;
    Dlg_head *h = l->widget.parent;
    cb_ret_t ret_code;

    switch (msg) {
    case WIDGET_INIT:
	return MSG_HANDLED;

    case WIDGET_HOTKEY:
    {
	int pos, action;

	pos = listbox_check_hotkey (l, parm);
	if (pos < 0)
	    return MSG_NOT_HANDLED;

	listbox_select_entry (l, pos);
	h->callback (h, w, DLG_ACTION, l->pos, NULL);

	if (l->cback != NULL)
	    action = l->cback (l);
	else
	    action = LISTBOX_DONE;

	if (action == LISTBOX_DONE) {
	    h->ret_value = B_ENTER;
	    dlg_stop (h);
	}

	return MSG_HANDLED;
    }

    case WIDGET_KEY:
	ret_code = listbox_key (l, parm);
	if (ret_code != MSG_NOT_HANDLED) {
	    listbox_draw (l, TRUE);
	    h->callback (h, w, DLG_ACTION, l->pos, NULL);
	}
	return ret_code;

    case WIDGET_CURSOR:
	widget_move (&l->widget, l->cursor_y, 0);
	h->callback (h, w, DLG_ACTION, l->pos, NULL);
	return MSG_HANDLED;

    case WIDGET_FOCUS:
    case WIDGET_UNFOCUS:
    case WIDGET_DRAW:
	listbox_draw (l, msg != WIDGET_UNFOCUS);
	return MSG_HANDLED;

    case WIDGET_DESTROY:
	listbox_destroy (l);
	return MSG_HANDLED;

    case WIDGET_RESIZED:
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

static int
listbox_event (Gpm_Event *event, void *data)
{
    WListbox *l = data;
    int i;

    Dlg_head *h = l->widget.parent;

    /* Single click */
    if (event->type & GPM_DOWN)
	dlg_select_widget (l);

    if (l->list == NULL)
	return MOU_NORMAL;

    if (event->type & (GPM_DOWN | GPM_DRAG)) {
	int ret = MOU_REPEAT;

	if (event->x < 0 || event->x > l->widget.cols)
	    return ret;

	if (event->y < 1)
	    for (i = -event->y; i >= 0; i--)
		listbox_back (l);
	else if (event->y > l->widget.lines)
	    for (i = event->y - l->widget.lines; i > 0; i--)
		listbox_fwd (l);
	else if (event->buttons & GPM_B_UP) {
	    listbox_back (l);
	    ret = MOU_NORMAL;
	} else if (event->buttons & GPM_B_DOWN) {
	    listbox_fwd (l);
	    ret = MOU_NORMAL;
	} else
	    listbox_select_entry (l, listbox_select_pos (l, l->top, event->y - 1));

	/* We need to refresh ourselves since the dialog manager doesn't */
	/* know about this event */
	listbox_draw (l, TRUE);
	return ret;
    }

    /* Double click */
    if ((event->type & (GPM_DOUBLE | GPM_UP)) == (GPM_UP | GPM_DOUBLE)) {
	int action;

	if (event->x < 0 || event->x >= l->widget.cols
	    || event->y < 1 || event->y > l->widget.lines)
	    return MOU_NORMAL;

	dlg_select_widget (l);
	listbox_select_entry (l, listbox_select_pos (l, l->top, event->y - 1));

	if (l->cback != NULL)
	    action = l->cback (l);
	else
	    action = LISTBOX_DONE;

	if (action == LISTBOX_DONE) {
	    h->ret_value = B_ENTER;
	    dlg_stop (h);
	    return MOU_NORMAL;
	}
    }
    return MOU_NORMAL;
}

WListbox *
listbox_new (int y, int x, int height, int width, gboolean deletable, lcback callback)
{
    WListbox *l = g_new (WListbox, 1);

    if (height <= 0)
	height = 1;

    init_widget (&l->widget, y, x, height, width,
		 listbox_callback, listbox_event);

    l->list = NULL;
    l->top = l->pos = 0;
    l->count = 0;
    l->deletable = deletable;
    l->cback = callback;
    l->allow_duplicates = TRUE;
    l->scrollbar = !tty_is_slow ();
    widget_want_hotkey (l->widget, 1);

    return l;
}

static int
listbox_entry_cmp (const void *a, const void *b)
{
    const WLEntry *ea = (const WLEntry *) a;
    const WLEntry *eb = (const WLEntry *) b;

    return strcmp (ea->text, eb->text);
}

/* Listbox item adding function */
static inline void
listbox_append_item (WListbox *l, WLEntry *e, listbox_append_t pos)
{
    switch (pos) {
    case LISTBOX_APPEND_AT_END:
	l->list = g_list_append (l->list, e);
	break;

    case LISTBOX_APPEND_BEFORE:
	l->list = g_list_insert_before (l->list, g_list_nth (l->list, l->pos), e);
	if (l->pos > 0)
	    l->pos--;
	break;

    case LISTBOX_APPEND_AFTER:
	l->list = g_list_insert (l->list, e, l->pos + 1);
	break;

    case LISTBOX_APPEND_SORTED:
	l->list = g_list_insert_sorted (l->list, e, (GCompareFunc) listbox_entry_cmp);
	break;

    default:
	return;
    }

    l->count++;
}

char *
listbox_add_item (WListbox *l, listbox_append_t pos, int hotkey,
		  const char *text, void *data)
{
    WLEntry *entry;

    if (l == NULL)
	return NULL;

    if (!l->allow_duplicates && (listbox_search_text (l, text) >= 0))
	return NULL;

    entry = g_new (WLEntry, 1);
    entry->text = g_strdup (text);
    entry->data = data;
    entry->hotkey = hotkey;

    listbox_append_item (l, entry, pos);

    return entry->text;
}

int
listbox_search_text (WListbox *l, const char *text)
{
    if (l != NULL) {
	int i;
	GList *le;

	for (i = 0, le = l->list; le != NULL; i++, le = g_list_next (le)) {
	    WLEntry *e = (WLEntry *) le->data;

	    if (strcmp (e->text, text) == 0)
		return i;
	}
    }

    return (-1);
}

/* Returns the current string text as well as the associated extra data */
void
listbox_get_current (WListbox *l, char **string, void **extra)
{
    WLEntry *e = NULL;
    gboolean ok;

    if (l != NULL)
	e = (WLEntry *) g_list_nth_data (l->list, l->pos);

    ok = (e != NULL);

    if (string != NULL)
	*string = ok ? e->text : NULL;

    if (extra != NULL)
	*extra = ok ? e->data : NULL;
}


/* ButtonBar widget */

/* returns TRUE if a function has been called, FALSE otherwise. */
static gboolean
buttonbar_call (WButtonBar *bb, int i)
{
    cb_ret_t ret = MSG_NOT_HANDLED;

    if (bb != NULL)
	ret = bb->widget.parent->callback (bb->widget.parent,
					    (Widget *) bb, DLG_ACTION,
					    bb->labels[i].command,
					    bb->labels[i].receiver);
    return ret;
}

/* calculate width of one button, width is never lesser than 7 */
static int
buttonbat_get_button_width (void)
{
    int result = COLS / BUTTONBAR_LABELS_NUM;
    return (result >= 7) ? result : 7;
}

static cb_ret_t
buttonbar_callback (Widget *w, widget_msg_t msg, int parm)
{
    WButtonBar *bb = (WButtonBar *) w;
    int i;
    const char *text;

    switch (msg) {
    case WIDGET_FOCUS:
	return MSG_NOT_HANDLED;

    case WIDGET_HOTKEY:
	for (i = 0; i < BUTTONBAR_LABELS_NUM; i++)
	    if (parm == KEY_F (i + 1) && buttonbar_call (bb, i))
		return MSG_HANDLED;
	return MSG_NOT_HANDLED;

    case WIDGET_DRAW:
	if (bb->visible) {
	    int offset = 0;
	    int count_free_positions;

	    widget_move (&bb->widget, 0, 0);
	    tty_setcolor (DEFAULT_COLOR);
	    bb->btn_width = buttonbat_get_button_width ();
	    tty_printf ("%-*s", bb->widget.cols, "");
	    count_free_positions = COLS - bb->btn_width * BUTTONBAR_LABELS_NUM;

	    for (i = 0; i < COLS / bb->btn_width && i < BUTTONBAR_LABELS_NUM; i++) {
		widget_move (&bb->widget, 0, (i * bb->btn_width) + offset);
		tty_setcolor (BUTTONBAR_HOTKEY_COLOR);
		tty_printf ("%2d", i + 1);
		tty_setcolor (BUTTONBAR_BUTTON_COLOR);
		text = (bb->labels[i].text != NULL) ? bb->labels[i].text : "";
		tty_print_string (str_fit_to_term (
			text,
			bb->btn_width - 2 + (int)(offset < count_free_positions),
			J_LEFT_FIT));

		if (count_free_positions != 0 && offset < count_free_positions)
		    offset++;
	    }
	}
	return MSG_HANDLED;

    case WIDGET_DESTROY:
	for (i = 0; i < BUTTONBAR_LABELS_NUM; i++)
	    g_free (bb->labels[i].text);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

static int
buttonbar_event (Gpm_Event *event, void *data)
{
    WButtonBar *bb = data;
    int button;

    if (!(event->type & GPM_UP))
	return MOU_NORMAL;
    if (event->y == 2)
	return MOU_NORMAL;
    button = (event->x - 1) * BUTTONBAR_LABELS_NUM / COLS;
    if (button < BUTTONBAR_LABELS_NUM)
	buttonbar_call (bb, button);
    return MOU_NORMAL;
}

WButtonBar *
buttonbar_new (gboolean visible)
{
    WButtonBar *bb;

    bb = g_new0 (WButtonBar, 1);

    init_widget (&bb->widget, LINES - 1, 0, 1, COLS,
		 buttonbar_callback, buttonbar_event);
    bb->widget.pos_flags = WPOS_KEEP_HORZ | WPOS_KEEP_BOTTOM;
    bb->visible = visible;
    widget_want_hotkey (bb->widget, 1);
    widget_want_cursor (bb->widget, 0);
    bb->btn_width = buttonbat_get_button_width ();

    return bb;
}

static void
set_label_text (WButtonBar *bb, int lc_index, const char *text)
{
    g_free (bb->labels[lc_index - 1].text);
    bb->labels[lc_index - 1].text = g_strdup (text);
}

/* Find ButtonBar widget in the dialog */
WButtonBar *
find_buttonbar (const Dlg_head *h)
{
    return (WButtonBar *) find_widget_type (h, buttonbar_callback);
}

void
buttonbar_set_label (WButtonBar *bb, int idx, const char *text,
			const struct global_keymap_t *keymap, const Widget *receiver)
{
    if ((bb != NULL)  && (idx >= 1) && (idx <= BUTTONBAR_LABELS_NUM)) {
	unsigned long command = CK_Ignore_Key;

	if (keymap != NULL)
	    command = lookup_keymap_command (keymap, KEY_F (idx));

	if ((text == NULL) || (text[0] == '\0'))
	    set_label_text (bb, idx, "");
	else
	    set_label_text (bb, idx, text);

	bb->labels[idx - 1].command = command;
	bb->labels[idx - 1].receiver = (Widget *) receiver;
    }
}

void
buttonbar_set_visible (WButtonBar *bb, gboolean visible)
{
    bb->visible = visible;
}

void
buttonbar_redraw (WButtonBar *bb)
{
    if (bb != NULL)
	send_message ((Widget *) bb, WIDGET_DRAW, 0);
}

static cb_ret_t
groupbox_callback (Widget *w, widget_msg_t msg, int parm)
{
    WGroupbox *g = (WGroupbox *) w;

    switch (msg) {
    case WIDGET_INIT:
	return MSG_HANDLED;

    case WIDGET_FOCUS:
	return MSG_NOT_HANDLED;

    case WIDGET_DRAW:
	tty_setcolor (COLOR_NORMAL);
	draw_box (g->widget.parent, g->widget.y - g->widget.parent->y,
		  g->widget.x - g->widget.parent->x, g->widget.lines,
		  g->widget.cols);

	tty_setcolor (COLOR_HOT_NORMAL);
	dlg_move (g->widget.parent, g->widget.y - g->widget.parent->y,
		  g->widget.x - g->widget.parent->x + 1);
	tty_print_string (g->title);
	return MSG_HANDLED;

    case WIDGET_DESTROY:
	g_free (g->title);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

WGroupbox *
groupbox_new (int y, int x, int height, int width, const char *title)
{
    WGroupbox *g = g_new (WGroupbox, 1);

    init_widget (&g->widget, y, x, height, width, groupbox_callback, NULL);

    g->widget.options &= ~W_WANT_CURSOR;
    widget_want_hotkey (g->widget, 0);

    /* Strip existing spaces, add one space before and after the title */
    if (title) {
	char *t;
	t = g_strstrip (g_strdup (title));
	g->title = g_strconcat (" ", t, " ", (char *) NULL);
	g_free (t);
    }

    return g;
}
