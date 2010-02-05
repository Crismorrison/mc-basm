/* editor initialisation and callback handler.

   Copyright (C) 1996, 1997, 1998, 2001, 2002, 2003, 2004, 2005, 2006,
   2007 Free Software Foundation, Inc.

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
 *  \brief Source: editor initialisation and callback handler
 *  \author Paul Sheer
 *  \date 1996, 1997
 */

#include <config.h>

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

#include "lib/tty/tty.h"		/* LINES, COLS */
#include "lib/tty/key.h"		/* is_idle() */

#include "edit-impl.h"
#include "edit-widget.h"

#include "src/dialog.h"
#include "src/widget.h"		/* ButtonBar */
#include "src/menu.h"		/* menubar_new() */
#include "src/cmddef.h"

WEdit *wedit;
struct WMenuBar *edit_menubar;

int column_highlighting = 0;

static cb_ret_t edit_callback (Widget *, widget_msg_t msg, int parm);

static char *
edit_get_shortcut (unsigned long command)
{
    const char *ext_map;
    const char *shortcut = NULL;

    shortcut = lookup_keymap_shortcut (editor_map, command);
    if (shortcut != NULL)
	return g_strdup (shortcut);

    ext_map = lookup_keymap_shortcut (editor_map, CK_Ext_Mode);
    if (ext_map != NULL)
	shortcut = lookup_keymap_shortcut (editor_x_map, command);
    if (shortcut != NULL)
	return g_strdup_printf ("%s %s", ext_map, shortcut);

    return NULL;
}

static int
edit_event (Gpm_Event *event, void *data)
{
    WEdit *edit = (WEdit *) data;

    /* Unknown event type */
    if (!(event->type & (GPM_DOWN | GPM_DRAG | GPM_UP)))
	return MOU_NORMAL;

    /* rest of the upper frame, the menu is invisible - call menu */
    if ((event->type & GPM_DOWN) && (event->y == 1))
	return edit_menubar->widget.mouse (event, edit_menubar);

    edit_update_curs_row (edit);
    edit_update_curs_col (edit);

    /* Outside editor window */
    if (event->y <= 1 || event->x <= 0
	|| event->x > edit->num_widget_columns
	|| event->y > edit->num_widget_lines + 1)
	return MOU_NORMAL;

    /* Wheel events */
    if ((event->buttons & GPM_B_UP) && (event->type & GPM_DOWN)) {
	edit_move_up (edit, 2, 1);
	goto update;
    }
    if ((event->buttons & GPM_B_DOWN) && (event->type & GPM_DOWN)) {
	edit_move_down (edit, 2, 1);
	goto update;
    }

    /* A lone up mustn't do anything */
    if (edit->mark2 != -1 && event->type & (GPM_UP | GPM_DRAG))
	return MOU_NORMAL;

    if (event->type & (GPM_DOWN | GPM_UP))
	edit_push_key_press (edit);

    if (option_cursor_beyond_eol) {
        long line_len = edit_move_forward3 (edit, edit_bol (edit, edit->curs1), 0,
                                            edit_eol(edit, edit->curs1));

        if ( event->x > line_len ) {
            edit->over_col = event->x - line_len - edit->start_col - option_line_state_width - 1;
            edit->prev_col = line_len;
        } else {
            edit->over_col = 0;
            edit->prev_col = event->x - option_line_state_width - edit->start_col - 1;
        }
    } else {
        edit->prev_col = event->x - edit->start_col - option_line_state_width - 1;
    }

    if (--event->y > (edit->curs_row + 1))
	edit_move_down (edit, event->y - (edit->curs_row + 1), 0);
    else if (event->y < (edit->curs_row + 1))
	edit_move_up (edit, (edit->curs_row + 1) - event->y, 0);
    else
	edit_move_to_prev_col (edit, edit_bol (edit, edit->curs1));

    if (event->type & GPM_DOWN) {
	edit_mark_cmd (edit, 1);	/* reset */
	edit->highlight = 0;
    }

    if (!(event->type & GPM_DRAG))
	edit_mark_cmd (edit, 0);

  update:
    edit_find_bracket (edit);
    edit->force |= REDRAW_COMPLETELY;
    edit_update_curs_row (edit);
    edit_update_curs_col (edit);
    edit_update_screen (edit);

    return MOU_NORMAL;
}

static cb_ret_t
edit_command_execute (WEdit *edit, unsigned long command)
{
    if (command == CK_Menu)
	edit_menu_cmd (edit);
    else {
	edit_execute_key_command (edit, command, -1);
	edit_update_screen (edit);
    }
    return MSG_HANDLED;
}

static inline void
edit_set_buttonbar (WEdit *edit, WButtonBar *bb)
{
    buttonbar_set_label (bb,  1, Q_("ButtonBar|Help"),   editor_map, (Widget *) edit);
    buttonbar_set_label (bb,  2, Q_("ButtonBar|Save"),   editor_map, (Widget *) edit);
    buttonbar_set_label (bb,  3, Q_("ButtonBar|Mark"),   editor_map, (Widget *) edit);
    buttonbar_set_label (bb,  4, Q_("ButtonBar|Replac"), editor_map, (Widget *) edit);
    buttonbar_set_label (bb,  5, Q_("ButtonBar|Copy"),   editor_map, (Widget *) edit);
    buttonbar_set_label (bb,  6, Q_("ButtonBar|Move"),   editor_map, (Widget *) edit);
    buttonbar_set_label (bb,  7, Q_("ButtonBar|Search"), editor_map, (Widget *) edit);
    buttonbar_set_label (bb,  8, Q_("ButtonBar|Delete"), editor_map, (Widget *) edit);
    buttonbar_set_label (bb,  9, Q_("ButtonBar|PullDn"), editor_map, (Widget *) edit);
    buttonbar_set_label (bb, 10, Q_("ButtonBar|Quit"),   editor_map, (Widget *) edit);
}

/* Callback for the edit dialog */
static cb_ret_t
edit_dialog_callback (Dlg_head *h, Widget *sender,
			dlg_msg_t msg, int parm, void *data)
{
    WEdit *edit;
    WMenuBar *menubar;
    WButtonBar *buttonbar;

    edit = (WEdit *) find_widget_type (h, edit_callback);
    menubar = find_menubar (h);
    buttonbar = find_buttonbar (h);

    switch (msg) {
    case DLG_INIT:
	edit_set_buttonbar (edit, buttonbar);
	return MSG_HANDLED;

    case DLG_RESIZE:
	widget_set_size (&edit->widget, 0, 0, LINES - 1, COLS);
	widget_set_size (&buttonbar->widget , LINES - 1, 0, 1, COLS);
	widget_set_size (&menubar->widget, 0, 0, 1, COLS);
	menubar_arrange (menubar);
	return MSG_HANDLED;

    case DLG_ACTION:
	if (sender == (Widget *) menubar)
	    return send_message ((Widget *) edit, WIDGET_COMMAND, parm);
	if (sender == (Widget *) buttonbar)
	    return send_message ((Widget *) edit, WIDGET_COMMAND, parm);
	return MSG_HANDLED;

    case DLG_VALIDATE:
	if (!edit_ok_to_exit (edit))
	    h->running = 1;
	return MSG_HANDLED;

    default:
	return default_dlg_callback (h, sender, msg, parm, data);
    }
}

int
edit_file (const char *_file, int line)
{
    static gboolean made_directory = FALSE;
    Dlg_head *edit_dlg;

    if (!made_directory) {
	char *dir = concat_dir_and_file (home_dir, EDIT_DIR);
	made_directory = (mkdir (dir, 0700) != -1 || errno == EEXIST);
	g_free (dir);
    }

    wedit = edit_init (NULL, LINES - 2, COLS, _file, line);

    if (wedit == NULL)
	return 0;

    /* Create a new dialog and add it widgets to it */
    edit_dlg =
	create_dlg (0, 0, LINES, COLS, NULL, edit_dialog_callback,
		    "[Internal File Editor]", NULL, DLG_WANT_TAB);

    edit_dlg->get_shortcut = edit_get_shortcut;
    edit_menubar = menubar_new (0, 0, COLS, NULL);
    add_widget (edit_dlg, edit_menubar);
    edit_init_menu (edit_menubar);

    init_widget (&(wedit->widget), 0, 0, LINES - 1, COLS,
		 edit_callback, edit_event);
    widget_want_cursor (wedit->widget, 1);

    add_widget (edit_dlg, wedit);

    add_widget (edit_dlg, buttonbar_new (TRUE));

    run_dlg (edit_dlg);

    destroy_dlg (edit_dlg);

    return 1;
}

const char *
edit_get_file_name (const WEdit *edit)
{
    return edit->filename;
}

void
edit_update_screen (WEdit * e)
{
    edit_scroll_screen_over_cursor (e);

    edit_update_curs_col (e);
    edit_status (e);

    /* pop all events for this window for internal handling */
    if (!is_idle ())
	e->force |= REDRAW_PAGE;
    else {
	if (e->force & REDRAW_COMPLETELY)
	    e->force |= REDRAW_PAGE;
	edit_render_keypress (e);
    }
}

static cb_ret_t
edit_callback (Widget *w, widget_msg_t msg, int parm)
{
    WEdit *e = (WEdit *) w;

    switch (msg) {
    case WIDGET_DRAW:
	e->force |= REDRAW_COMPLETELY;
	e->num_widget_lines = LINES - 2;
	e->num_widget_columns = COLS;
	/* fallthrough */

    case WIDGET_FOCUS:
	edit_update_screen (e);
	return MSG_HANDLED;

    case WIDGET_KEY:
	{
	    int cmd, ch;
	    cb_ret_t ret = MSG_NOT_HANDLED;

	    /* The user may override the access-keys for the menu bar. */
	    if (edit_translate_key (e, parm, &cmd, &ch)) {
		edit_execute_key_command (e, cmd, ch);
		edit_update_screen (e);
		ret = MSG_HANDLED;
	    } else  if (edit_drop_hotkey_menu (e, parm))
		ret =  MSG_HANDLED;

	    return ret;
	}

    case WIDGET_COMMAND:
	/* command from menubar or buttonbar */
	return edit_command_execute (e, parm);

    case WIDGET_CURSOR:
	widget_move (&e->widget, e->curs_row + EDIT_TEXT_VERTICAL_OFFSET,
		     e->curs_col + e->start_col + e->over_col +
		     EDIT_TEXT_HORIZONTAL_OFFSET + option_line_state_width);
	return MSG_HANDLED;

    case WIDGET_DESTROY:
	edit_clean (e);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}
