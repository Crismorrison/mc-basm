/* Panel layout module for the Midnight Commander
   Copyright (C) 1995, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
   2006, 2007, 2009 Free Software Foundation, Inc.
   
   Written: 1995 Janne Kukonlehto
            1995 Miguel de Icaza

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

/** \file layout.c
 *  \brief Source: panel layout module
 */

#include <config.h>

#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

     /*
      * If TIOCGWINSZ supported, make it available here, because window-
      * resizing code depends on it...
      */
#ifdef HAVE_SYS_IOCTL_H
#   include <sys/ioctl.h>
#endif
#include <termios.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/tty/tty.h"
#include "lib/skin.h"
#include "lib/tty/key.h"
#include "lib/tty/mouse.h"
#include "lib/tty/win.h"	/* do_enter_ca_mode() */
#include "lib/mcconfig.h"
#include "lib/vfs/mc-vfs/vfs.h"		/* For vfs_translate_url() */
#include "lib/strutil.h"

#include "dialog.h"
#include "widget.h"
#include "command.h"
#include "main-widgets.h"
#include "main.h"
#include "subshell.h"	/* For use_subshell and resize_subshell() */
#include "tree.h"
#include "menu.h"
#include "background.h"         /* we_are_background */
/* Needed for the extern declarations of integer parameters */
#include "dir.h"
#include "panel.h"		/* The Panel widget */
#include "consaver/cons.saver.h"
#include "layout.h"
#include "info.h"		/* The Info widget */
#include "src/viewer/mcviewer.h"     /* The view widget */

#include "setup.h"		/* For save_setup() */

/* Controls the display of the rotating dash on the verbose mode */
int nice_rotating_dash = 1;

/* Set if the panels are split horizontally */
int horizontal_split = 0;

/* Set if the window has changed it's size */
int winch_flag = 0;

/* Set if the split is the same */
int equal_split = 1;

/* First panel size if the panel are not split equally */
int first_panel_size = 0;

/* The number of output lines shown (if available) */
int output_lines = 0;

/* Set if the command prompt is to be displayed */
int command_prompt = 1;

/* Set if the nice and useful keybar is visible */
int keybar_visible = 1;

/* Set if the nice message (hint) bar is visible */
int message_visible = 1;

/* Set to show current working dir in xterm window title */
int xterm_title = 1;

/* Set to show free space on device assigned to current directory */
int free_space = 1;

/* The starting line for the output of the subprogram */
int output_start_y = 0;

/* The maximum number of views managed by the set_display_type routine */
/* Must be at least two (for current and other).  Please note that until */
/* Janne gets around this, we will only manage two of them :-) */
#define MAX_VIEWS 2

static struct {
    panel_view_mode_t type;
    Widget *widget;
    char *last_saved_dir;  /* last view_list working directory */
} panels [MAX_VIEWS] = {
    /* init MAX_VIEWS items */
    { view_listing, NULL, NULL },
    { view_listing, NULL, NULL }
};

/* These variables are used to avoid updating the information unless */
/* we need it */
static int old_first_panel_size;
static int old_horizontal_split;
static int old_output_lines;

/* Internal variables */
static int _horizontal_split;
static int _equal_split;
static int _first_panel_size;
static int _menubar_visible;
static int _output_lines;
static int _command_prompt;
static int _keybar_visible;
static int _message_visible;
static int _xterm_title;
static int _free_space;
static int _permission_mode;
static int _filetype_mode;

static int height;

/* Width 12 for a wee Quick (Hex) View */
#define MINWIDTH 12
#define MINHEIGHT 5

#define BY      12

#define B_2LEFT B_USER
#define B_2RIGHT (B_USER + 1)
#define B_PLUS (B_USER + 2)
#define B_MINUS (B_USER + 3)

static Dlg_head *layout_dlg;

static const char *s_split_direction [2] = {
    N_("&Vertical"), 
    N_("&Horizontal")
};

static WRadio *radio_widget;

static struct {
    const char   *text;
    int    *variable;
    WCheck *widget;
} check_options [] = {
    { N_("show free sp&Ace"),  &free_space,      0 },
    { N_("&Xterm window title"), &xterm_title,   0 },
    { N_("h&Intbar visible"),  &message_visible, 0 },
    { N_("&Keybar visible"),   &keybar_visible,  0 },
    { N_("command &Prompt"),   &command_prompt,  0 },
    { N_("show &Mini status"), &show_mini_info,  0 },
    { N_("menu&Bar visible"),  &menubar_visible, 0 },
    { N_("&Equal split"),      &equal_split,     0 },
    { N_("pe&Rmissions"),      &permission_mode, 0 },
    { N_("&File types"),       &filetype_mode,   0 },
    { 0, 0, 0 }
};

#define LAYOUT_OPTIONS_COUNT           10
#define HIGHLIGHT_OPTIONS_COUNT        2
#define SPLIT_OPTIONS_COUNT            1
#define OTHER_OPTIONS_COUNT            7

static gsize first_width, second_width;
static const char *output_lines_label;

static WButton *bleft_widget, *bright_widget;

static void _check_split (void)
{
    if (_horizontal_split){
	if (_equal_split)
	    _first_panel_size = height / 2;
	else if (_first_panel_size < MINHEIGHT)
	    _first_panel_size = MINHEIGHT;
	else if (_first_panel_size > height - MINHEIGHT)
	    _first_panel_size = height - MINHEIGHT;
    } else {
	if (_equal_split)
	    _first_panel_size = COLS / 2;
	else if (_first_panel_size < MINWIDTH)
	    _first_panel_size = MINWIDTH;
	else if (_first_panel_size > COLS - MINWIDTH)
	    _first_panel_size = COLS - MINWIDTH;
    }
}

static void update_split (void)
{
    /* Check split has to be done before testing if it changed, since
       it can change due to calling _check_split() as well*/
    _check_split ();
    
    /* To avoid setting the cursor to the wrong place */
    if ((old_first_panel_size == _first_panel_size) &&
	(old_horizontal_split == _horizontal_split)){
	return;
    }

    old_first_panel_size = _first_panel_size;
    old_horizontal_split = _horizontal_split; 
   
    tty_setcolor (COLOR_NORMAL);
    dlg_move (layout_dlg, 6, 6);
    tty_printf ("%03d", _first_panel_size);
    dlg_move (layout_dlg, 6, 18);
    if (_horizontal_split)
	tty_printf ("%03d", height - _first_panel_size);
    else
	tty_printf ("%03d", COLS - _first_panel_size);
}

static int b2left_cback (int action)
{
    (void) action;

    if (_equal_split){
	/* Turn equal split off */
	_equal_split = 0;
	check_options [7].widget->state = check_options [7].widget->state & ~C_BOOL;
	dlg_select_widget (check_options [7].widget);
	dlg_select_widget (bleft_widget);
    }
    _first_panel_size++;
    return 0;
}

static int b2right_cback (int action)
{
    (void) action;

    if (_equal_split){
	/* Turn equal split off */
	_equal_split = 0;
	check_options [7].widget->state = check_options [7].widget->state & ~C_BOOL;
	dlg_select_widget (check_options [7].widget);
	dlg_select_widget (bright_widget);
    }
    _first_panel_size--;
    return 0;
}

static int bplus_cback (int action)
{
    (void) action;

    if (_output_lines < 99)
	_output_lines++;
    return 0;
}

static int bminus_cback (int action)
{
    (void) action;

    if (_output_lines > 0)
	_output_lines--;
    return 0;
}

static cb_ret_t
layout_callback (Dlg_head *h, Widget *sender,
		    dlg_msg_t msg, int parm, void *data)
{
    switch (msg) {
    case DLG_DRAW:
	/*When repainting the whole dialog (e.g. with C-l) we have to
	  update everything*/
	common_dialog_repaint (h);

	old_first_panel_size = -1;
	old_horizontal_split = -1;
	old_output_lines     = -1;

	tty_setcolor (COLOR_HOT_NORMAL);
	update_split ();
	dlg_move (h, 6, 13);
	tty_print_char ('=');
	if (console_flag){
	    if (old_output_lines != _output_lines){
		old_output_lines = _output_lines;
		tty_setcolor (COLOR_NORMAL);
		dlg_move (h, LAYOUT_OPTIONS_COUNT, 16 + first_width);
		tty_print_string (output_lines_label);
		dlg_move (h, LAYOUT_OPTIONS_COUNT, 10 + first_width);
		tty_printf ("%02d", _output_lines);
	    }
	}
	return MSG_HANDLED;

    case DLG_POST_KEY:
	_filetype_mode = check_options [9].widget->state & C_BOOL;
	_permission_mode = check_options [8].widget->state & C_BOOL;
	_equal_split = check_options [7].widget->state & C_BOOL;
	_menubar_visible = check_options [6].widget->state & C_BOOL;
	_command_prompt = check_options [5].widget->state & C_BOOL;
	_keybar_visible = check_options [3].widget->state & C_BOOL;
	_message_visible = check_options [2].widget->state & C_BOOL;
	_xterm_title = check_options [1].widget->state & C_BOOL;
	_free_space = check_options [0].widget->state & C_BOOL;
	if (console_flag){
	    int minimum;
	    if (_output_lines < 0)
		_output_lines = 0;
	    height = LINES - _keybar_visible - _command_prompt -
		     _menubar_visible - _output_lines - _message_visible;
	    minimum = MINHEIGHT * (1 + _horizontal_split);
	    if (height < minimum){
		_output_lines -= minimum - height;
		height = minimum;
	    }
	} else {
	    height = LINES - _keybar_visible - _command_prompt -
		_menubar_visible - _output_lines - _message_visible;
	}
	if (_horizontal_split != radio_widget->sel){
	    _horizontal_split = radio_widget->sel;
	    if (_horizontal_split)
		_first_panel_size = height / 2;
	    else
		_first_panel_size = COLS / 2;
	}
	update_split ();
	if (console_flag){
	    if (old_output_lines != _output_lines){
		old_output_lines = _output_lines;
		tty_setcolor (COLOR_NORMAL);
		dlg_move (h, LAYOUT_OPTIONS_COUNT, 10 + first_width);
		tty_printf ("%02d", _output_lines);
	    }
	}
	return MSG_HANDLED;

    default:
        return default_dlg_callback (h, sender, msg, parm, data);
    }
}

static void
init_layout (void)
{
    static int i18n_layt_flag = 0;
    static int b1, b2, b3;
    int i = sizeof (s_split_direction) / sizeof (char *);
    const char *ok_button = _("&OK");
    const char *cancel_button = _("&Cancel");
    const char *save_button = _("&Save");
    static const char *title1, *title2, *title3;

    if (!i18n_layt_flag) {
	gsize l1;

	first_width = 19;	/* length of line with '<' '>' buttons */

	title1 = _(" Panel split ");
	title2 = _(" Highlight... ");
	title3 = _(" Other options ");
	output_lines_label = _("output lines");

	while (i--) {
	    s_split_direction[i] = _(s_split_direction[i]);
            l1 = str_term_width1 (s_split_direction[i]) + 7;
	    if (l1 > first_width)
		first_width = l1;
	}

	for (i = 0; i < LAYOUT_OPTIONS_COUNT; i++) {
	    check_options[i].text = _(check_options[i].text);
            l1 = str_term_width1 (check_options[i].text) + 7;
	    if (l1 > first_width)
		first_width = l1;
	}

        l1 = str_term_width1 (title1) + 1;
	if (l1 > first_width)
	    first_width = l1;

        l1 = str_term_width1 (title2) + 1;
	if (l1 > first_width)
	    first_width = l1;

        second_width = str_term_width1 (title3) + 1;
	for (i = 0; i < OTHER_OPTIONS_COUNT; i++) {
	    check_options[i].text = _(check_options[i].text);
            l1 = str_term_width1 (check_options[i].text) + 7;
	    if (l1 > second_width)
		second_width = l1;
	}
	if (console_flag) {
            l1 = str_term_width1 (output_lines_label) + 13;
	    if (l1 > second_width)
		second_width = l1;
	}

	/*
	 * alex@bcs.zp.ua:
	 * To be completely correct, one need to check if the title
	 * does not exceed dialog length and total length of 3 buttons
	 * allows their placement in one row. But assuming this dialog
	 * is wide enough, I don't include such a tests.
	 *
	 * Now the last thing to do - properly space buttons...
	 */
        l1 = 11 + str_term_width1 (ok_button)	/* 14 - all brackets and inner space */
                + str_term_width1 (save_button)	/* notice: it is 3 char less because */
                + str_term_width1 (cancel_button);	/* of '&' char in button text */

	i = (first_width + second_width - l1) / 4;
	b1 = 5 + i;
        b2 = b1 + str_term_width1 (ok_button) + i + 6;
        b3 = b2 + str_term_width1 (save_button) + i + 4;

	i18n_layt_flag = 1;
    }

    layout_dlg =
	create_dlg (0, 0, 15, first_width + second_width + 9,
		    dialog_colors, layout_callback, "[Layout]",
		    _("Layout"), DLG_CENTER | DLG_REVERSE);

    add_widget (layout_dlg, groupbox_new (2, 4, 6, first_width, title1));
    add_widget (layout_dlg, groupbox_new (8, 4, 4, first_width, title2));
    add_widget (layout_dlg,
		groupbox_new (2, 5 + first_width, 10, second_width,
			      title3));

    add_widget (layout_dlg,
		button_new (BY, b3, B_CANCEL, NORMAL_BUTTON, cancel_button,
			    0));
    add_widget (layout_dlg,
		button_new (BY, b2, B_EXIT, NORMAL_BUTTON, save_button,
			    0));
    add_widget (layout_dlg,
		button_new (BY, b1, B_ENTER, DEFPUSH_BUTTON, ok_button,
			    0));
    if (console_flag) {
	add_widget (layout_dlg,
		    button_new (LAYOUT_OPTIONS_COUNT, 12 + first_width, B_MINUS,
				NARROW_BUTTON, "&-", bminus_cback));
	add_widget (layout_dlg,
		    button_new (LAYOUT_OPTIONS_COUNT, 7 + first_width, B_PLUS, NARROW_BUTTON,
				"&+", bplus_cback));
    }
#define XTRACT(i) *check_options[i].variable, check_options[i].text

    for (i = 0; i < OTHER_OPTIONS_COUNT; i++) {
	check_options[i].widget =
	    check_new (LAYOUT_OPTIONS_COUNT - i - 1, 7 + first_width, XTRACT (i));
	add_widget (layout_dlg, check_options[i].widget);
    }
    check_options[9].widget = check_new (10, 6, XTRACT (9));
    add_widget (layout_dlg, check_options[9].widget);
    check_options[8].widget = check_new (9, 6, XTRACT (8));
    add_widget (layout_dlg, check_options[8].widget);

    _filetype_mode = filetype_mode;
    _permission_mode = permission_mode;
    _equal_split = equal_split;
    _menubar_visible = menubar_visible;
    _command_prompt = command_prompt;
    _keybar_visible = keybar_visible;
    _message_visible = message_visible;
    _xterm_title = xterm_title;
    _free_space = free_space;
    bright_widget =
	button_new (6, 15, B_2RIGHT, NARROW_BUTTON, "&>", b2right_cback);
    add_widget (layout_dlg, bright_widget);
    bleft_widget =
	button_new (6, 9, B_2LEFT, NARROW_BUTTON, "&<", b2left_cback);
    add_widget (layout_dlg, bleft_widget);
    check_options[7].widget = check_new (5, 6, XTRACT (7));
    old_first_panel_size = -1;
    old_horizontal_split = -1;
    old_output_lines = -1;

    _first_panel_size = first_panel_size;
    _output_lines = output_lines;
    add_widget (layout_dlg, check_options[7].widget);
    radio_widget = radio_new (3, 6, 2, s_split_direction);
    add_widget (layout_dlg, radio_widget);
    radio_widget->sel = horizontal_split;
}

void
layout_change (void)
{
    setup_panels ();
    /* re-init the menu, because perhaps there was a change in the way 
       how the panel are split (horizontal/vertical). */
    done_menu ();
    init_menu ();
    menubar_arrange (the_menubar);
    load_hint (1);
}

void layout_cmd (void)
{
    int result;
    int i;
    int layout_do_change = 0;

    init_layout ();
    run_dlg (layout_dlg);
    result = layout_dlg->ret_value;

    if (result == B_ENTER || result == B_EXIT){
	for (i = 0; check_options [i].text; i++)
	    if (check_options [i].widget)
		*check_options [i].variable = check_options [i].widget->state & C_BOOL;
	horizontal_split = radio_widget->sel;
	first_panel_size = _first_panel_size;
	output_lines = _output_lines;
	layout_do_change = 1;
    }
    if (result == B_EXIT){
	save_layout ();
	mc_config_save_file (mc_main_config, NULL);
    }

    destroy_dlg (layout_dlg);
    if (layout_do_change)
	layout_change ();
}

static void check_split (void)
{
    if (horizontal_split){
	if (equal_split)
	    first_panel_size = height / 2;
	else if (first_panel_size < MINHEIGHT)
	    first_panel_size = MINHEIGHT;
	else if (first_panel_size > height - MINHEIGHT)
	    first_panel_size = height - MINHEIGHT;
    } else {
	if (equal_split)
	    first_panel_size = COLS / 2;
	else if (first_panel_size < MINWIDTH)
	    first_panel_size = MINWIDTH;
	else if (first_panel_size > COLS - MINWIDTH)
	    first_panel_size = COLS - MINWIDTH;
    }
}

void
clr_scr (void)
{
    tty_set_normal_attrs ();
    tty_fill_region (0, 0, LINES, COLS, ' ');
    tty_refresh ();
}

void
repaint_screen (void)
{
    do_refresh ();
    tty_refresh ();
}

void
mc_refresh (void)
{
#ifdef WITH_BACKGROUND
    if (we_are_background)
	return;
#endif				/* WITH_BACKGROUND */
    if (winch_flag == 0)
	tty_refresh ();
    else {
	/* if winch was caugth, we should do not only redraw screen, but
	    reposition/resize all */
	change_screen_size ();
    }
}

static void
panel_do_cols (int idx)
{
    if (get_display_type (idx) == view_listing)
	set_panel_formats ((WPanel *) panels [idx].widget);
    else
	panel_update_cols (panels [idx].widget, frame_half);
}

void
setup_panels (void)
{
    int start_y;
    int promptl;		/* the prompt len */

    if (console_flag) {
	int minimum;
	if (output_lines < 0)
	    output_lines = 0;
	height =
	    LINES - keybar_visible - command_prompt - menubar_visible -
	    output_lines - message_visible;
	minimum = MINHEIGHT * (1 + horizontal_split);
	if (height < minimum) {
	    output_lines -= minimum - height;
	    height = minimum;
	}
    } else {
	height =
	    LINES - menubar_visible - command_prompt - keybar_visible -
	    message_visible;
    }
    check_split ();
    start_y = menubar_visible;

    /* The column computing is defered until panel_do_cols */
    if (horizontal_split) {
	widget_set_size (panels[0].widget, start_y, 0, first_panel_size,
			 0);

	widget_set_size (panels[1].widget, start_y + first_panel_size, 0,
			 height - first_panel_size, 0);
    } else {
	int first_x = first_panel_size;

	widget_set_size (panels[0].widget, start_y, 0, height, 0);

	widget_set_size (panels[1].widget, start_y, first_x, height, 0);

    }
    panel_do_cols (0);
    panel_do_cols (1);

    promptl = str_term_width1 (mc_prompt);

    widget_set_size (&the_menubar->widget, 0, 0, 1, COLS);

    if (command_prompt) {
	widget_set_size (&cmdline->widget, LINES - 1 - keybar_visible,
			 promptl, 1, COLS - promptl);
	winput_set_origin (cmdline, promptl, COLS - promptl);
	widget_set_size (&the_prompt->widget, LINES - 1 - keybar_visible,
			 0, 1, promptl);
    } else {
	widget_set_size (&cmdline->widget, 0, 0, 0, 0);
	winput_set_origin (cmdline, 0, 0);
	widget_set_size (&the_prompt->widget, LINES, COLS, 0, 0);
    }

    widget_set_size (&the_bar->widget, LINES - 1, 0, keybar_visible, COLS);
    buttonbar_set_visible (the_bar, keybar_visible);

    /* Output window */
    if (console_flag && output_lines) {
	output_start_y =
	    LINES - command_prompt - keybar_visible - output_lines;
	show_console_contents (output_start_y,
			       LINES - output_lines - keybar_visible - 1,
			       LINES - keybar_visible - 1);
    }
    if (message_visible)
	widget_set_size (&the_hint->widget, height + start_y, 0, 1, COLS);
    else
	widget_set_size (&the_hint->widget, 0, 0, 0, 0);

    update_xterm_title_path ();
}

static inline void
low_level_change_screen_size (void)
{
#if defined(HAVE_SLANG) || NCURSES_VERSION_MAJOR >= 4
#if defined TIOCGWINSZ
    struct winsize winsz;

    winsz.ws_col = winsz.ws_row = 0;
    /* Ioctl on the STDIN_FILENO */
    ioctl (0, TIOCGWINSZ, &winsz);
    if (winsz.ws_col && winsz.ws_row){
#if defined(NCURSES_VERSION) && defined(HAVE_RESIZETERM)
	resizeterm(winsz.ws_row, winsz.ws_col);
	clearok(stdscr,TRUE);	/* sigwinch's should use a semaphore! */
#else
	COLS = winsz.ws_col;
	LINES = winsz.ws_row;
#endif
#ifdef HAVE_SUBSHELL_SUPPORT
	resize_subshell ();
#endif
    }
#endif /* TIOCGWINSZ */
#endif /* defined(HAVE_SLANG) || NCURSES_VERSION_MAJOR >= 4 */
}

void
sigwinch_handler (int dummy)
{
    (void) dummy;
#if !(defined(USE_NCURSES) || defined(USE_NCURSESW))	/* don't do malloc in a signal handler */
    low_level_change_screen_size ();
#endif
    winch_flag = 1;
}

void
change_screen_size (void)
{
    Dlg_head *d;

    winch_flag = 0;
#if defined(HAVE_SLANG) || NCURSES_VERSION_MAJOR >= 4
#if defined TIOCGWINSZ

#ifndef NCURSES_VERSION
    tty_noraw_mode ();
    tty_reset_screen ();
#endif
    low_level_change_screen_size ();
#ifdef HAVE_SLANG
    /* XSI Curses spec states that portable applications shall not invoke
     * initscr() more than once.  This kludge could be done within the scope
     * of the specification by using endwin followed by a refresh (in fact,
     * more than one curses implementation does this); it is guaranteed to work
     * only with slang.
     */
    SLsmg_init_smg ();
    do_enter_ca_mode ();
    tty_keypad (TRUE);
    tty_nodelay (FALSE);
#endif

    /* Inform all running dialogs */
    d = current_dlg;
    while (d != NULL) {
	(*d->callback) (d, NULL, DLG_RESIZE, 0, NULL);
	d = d->parent;
    }

    /* Now, force the redraw */
    repaint_screen ();
#endif				/* TIOCGWINSZ */
#endif				/* defined(HAVE_SLANG) || NCURSES_VERSION_MAJOR >= 4 */
}

static int ok_to_refresh = 1;

void use_dash (int flag)
{
    if (flag)
	ok_to_refresh++;
    else
	ok_to_refresh--;
}

void set_hintbar(const char *str) 
{
    label_set_text (the_hint, str);
    if (ok_to_refresh > 0)
        mc_refresh();
}

void print_vfs_message (const char *msg, ...)
{
    va_list ap;
    char str [128];

    va_start (ap, msg);
    g_vsnprintf (str, sizeof (str), msg, ap);
    va_end (ap);

    if (midnight_shutdown)
	return;

    if (!message_visible || !the_hint || !the_hint->widget.parent) {
	int col, row;

	if (!nice_rotating_dash || (ok_to_refresh <= 0))
	    return;

	/* Preserve current cursor position */
	tty_getyx (&row, &col);

	tty_gotoyx (0, 0);
	tty_setcolor (NORMAL_COLOR);
	tty_print_string (str_fit_to_term (str, COLS - 1, J_LEFT));

	/* Restore cursor position */
	tty_gotoyx (row, col);
	mc_refresh ();
	return;
    }

    if (message_visible) {
        set_hintbar(str);
    }
}

void rotate_dash (void)
{
    static const char rotating_dash [] = "|/-\\";
    static size_t pos = 0;

    if (!nice_rotating_dash || (ok_to_refresh <= 0))
	return;

    if (pos >= sizeof (rotating_dash)-1)
	pos = 0;
    tty_gotoyx (0, COLS - 1);
    tty_setcolor (NORMAL_COLOR);
    tty_print_char (rotating_dash [pos]);
    mc_refresh ();
    pos++;
}

const char *get_nth_panel_name (int num)
{
    static char buffer [BUF_SMALL];
    
    if (!num)
        return "New Left Panel";
    else if (num == 1)
        return "New Right Panel";
    else {
        g_snprintf (buffer, sizeof (buffer), "%ith Panel", num);
        return buffer;
    }
}

/* I wonder if I should start to use the folding mode than Dugan uses */
/*                                                                     */
/* This is the centralized managing of the panel display types         */
/* This routine takes care of destroying and creating new widgets      */
/* Please note that it could manage MAX_VIEWS, not just left and right */
/* Currently nothing in the code takes advantage of this and has hard- */
/* coded values for two panels only                                    */

/* Set the num-th panel to the view type: type */
/* This routine also keeps at least one WPanel object in the screen */
/* since a lot of routines depend on the current_panel variable */
void
set_display_type (int num, panel_view_mode_t type)
{
    int x = 0, y = 0, cols = 0, lines = 0;
    int the_other = 0;		/* Index to the other panel */
    const char *file_name = NULL;	/* For Quick view */
    Widget *new_widget = NULL, *old_widget = NULL;
    WPanel *the_other_panel = NULL;

    if (num >= MAX_VIEWS){
	fprintf (stderr, "Cannot allocate more that %d views\n", MAX_VIEWS);
	abort ();
    }
    /* Check that we will have a WPanel * at least */
    if (type != view_listing) {
	the_other = num == 0 ? 1 : 0;

	if (panels [the_other].type != view_listing)
	    return;
    }

    /* Get rid of it */
    if (panels [num].widget) {
	Widget *w = panels [num].widget;
	WPanel *panel = (WPanel *) panels [num].widget;

	x = w->x;
	y = w->y;
	cols  = w->cols;
	lines = w->lines;
	old_widget = panels [num].widget;

	if (panels [num].type == view_listing) {
	    if (panel->frame_size == frame_full && type != view_listing) {
		cols = COLS - first_panel_size;
		if (num == 1)
		    x = first_panel_size;
	    }
	}
    }

    /* Restoring saved path from panels.ini for nonlist panel */
    /* when it's first creation (for example view_info) */
    if (old_widget == NULL && type != view_listing) {
	char panel_dir [MC_MAXPATHLEN];
	mc_get_current_wd (panel_dir, sizeof (panel_dir));
	panels[num].last_saved_dir = g_strdup (panel_dir);
    }

    switch (type) {
    case view_nothing:
    case view_listing:
	new_widget = restore_into_right_dir_panel (num, old_widget);
	break;
	
    case view_info:
	new_widget = (Widget *) info_new ();
	break;

    case view_tree:
	new_widget = (Widget *) tree_new (1, 0, 0, 0, 0);
	break;

    case view_quick:
	new_widget = (Widget *) mcview_new (0, 0, 0, 0, 1);
	the_other_panel = (WPanel *) panels [the_other].widget;
	if (the_other_panel)
	    file_name = the_other_panel->dir.list[the_other_panel->selected].fname;
	else
	    file_name = "";
	
	mcview_load ((struct mcview_struct *) new_widget, 0, file_name, 0);
	break;
    }

    if (type != view_listing)
	/* Must save dir, for restoring after change type to */
	/* view_listing */
	save_panel_dir (num);

    panels [num].type = type;
    panels [num].widget = new_widget;

    /* We set the same size the old widget had */
    widget_set_size (new_widget, y, x, lines, cols);

    /* We use replace to keep the circular list of the dialog in the */
    /* same state.  Maybe we could just kill it and then replace it  */
    if ((midnight_dlg != NULL) && (old_widget != NULL))
	dlg_replace_widget (old_widget, panels [num].widget);

    if (type == view_listing) {
	if (num == 0)
	    left_panel = (WPanel *) new_widget;
	else
	    right_panel = (WPanel *) new_widget;
    }

    if (type == view_tree)
	the_tree = (WTree *) new_widget;

    /* Prevent current_panel's value from becoming invalid.
     * It's just a quick hack to prevent segfaults. Comment out and
     * try following:
     * - select left panel
     * - invoke menue left/tree
     * - as long as you stay in the left panel almost everything that uses
     *   current_panel causes segfault, e.g. C-Enter, C-x c, ...
     */

    if ((type != view_listing)
	&& (current_panel == (WPanel *) old_widget))
	    current_panel = num == 0 ? right_panel : left_panel;
}

/* This routine is deeply sticked to the two panels idea.
   What should it do in more panels. ANSWER - don't use it
   in any multiple panels environment. */
void swap_panels ()
{
    Widget tmp;
    Widget *tmp_widget;
    WPanel panel;
    WPanel *panel1, *panel2;
    int tmp_type;

#define panelswap(x) panel. x = panel1-> x; panel1-> x = panel2-> x; panel2-> x = panel. x;

#define panelswapstr(e) strcpy (panel. e, panel1-> e); \
                        strcpy (panel1-> e, panel2-> e); \
                        strcpy (panel2-> e, panel. e);
    panel1 = (WPanel *) panels [0].widget;
    panel2 = (WPanel *) panels [1].widget;
    if (panels [0].type == view_listing && panels [1].type == view_listing) {
        /* Change everything except format/sort/panel_name etc. */
        panelswap (dir);
        panelswap (active);
        panelswapstr (cwd);
        panelswapstr (lwd);
        panelswap (count);
        panelswap (marked);
        panelswap (dirs_marked);
        panelswap (total);
        panelswap (top_file);
        panelswap (selected);
        panelswap (is_panelized);
        panelswap (dir_stat);

        panel1->searching = 0;
        panel2->searching = 0;
        if (current_panel == panel1)
            current_panel = panel2;
        else
            current_panel = panel1;

        if (dlg_widget_active (panels[0].widget))
            dlg_select_widget (panels [1].widget);
        else if (dlg_widget_active (panels[1].widget))
            dlg_select_widget (panels [0].widget);
    } else {
	WPanel *tmp_panel;
	
	tmp_panel = right_panel;
	right_panel = left_panel;
	left_panel = tmp_panel;
	
	if (panels [0].type == view_listing) {
            if (!strcmp (panel1->panel_name, get_nth_panel_name (0))) {
                g_free (panel1->panel_name);
                panel1->panel_name = g_strdup (get_nth_panel_name (1));
            }
        }
        if (panels [1].type == view_listing) {
            if (!strcmp (panel2->panel_name, get_nth_panel_name (1))) {
                g_free (panel2->panel_name);
                panel2->panel_name = g_strdup (get_nth_panel_name (0));
            }
        }

        tmp.x = panels [0].widget->x;
        tmp.y = panels [0].widget->y;
        tmp.cols = panels [0].widget->cols;
        tmp.lines = panels [0].widget->lines;

        panels [0].widget->x = panels [1].widget->x;
        panels [0].widget->y = panels [1].widget->y;
        panels [0].widget->cols = panels [1].widget->cols;
        panels [0].widget->lines = panels [1].widget->lines;

        panels [1].widget->x = tmp.x;
        panels [1].widget->y = tmp.y;
        panels [1].widget->cols = tmp.cols;
        panels [1].widget->lines = tmp.lines;

        tmp_widget = panels [0].widget;
        panels [0].widget = panels [1].widget;
        panels [1].widget = tmp_widget;
        tmp_type = panels [0].type;
        panels [0].type = panels [1].type;
        panels [1].type = tmp_type;
    }
}

panel_view_mode_t
get_display_type (int idx)
{
    return panels [idx].type;
}

struct Widget *
get_panel_widget (int idx)
{
    return panels[idx].widget;
}

int get_current_index (void)
{
    if (panels [0].widget == ((Widget *) current_panel))
	return 0;
    else
	return 1;
}

int get_other_index (void)
{
    return !get_current_index ();
}

struct WPanel *
get_other_panel (void)
{
    return (struct WPanel *) get_panel_widget (get_other_index ());
}

/* Returns the view type for the current panel/view */
panel_view_mode_t
get_current_type (void)
{
    if (panels [0].widget == (Widget *) current_panel)
	return panels [0].type;
    else
	return panels [1].type;
}

/* Returns the view type of the unselected panel */
panel_view_mode_t
get_other_type (void)
{
    if (panels [0].widget == (Widget *) current_panel)
	return panels [1].type;
    else
	return panels [0].type;
}

/* Save current list_view widget directory into panel */
void
save_panel_dir (int idx)
{
    panel_view_mode_t type = get_display_type (idx);
    Widget *widget = get_panel_widget (idx);

    if ((type == view_listing) && (widget != NULL)) {
	WPanel *w = (WPanel *) widget;
	char *widget_work_dir = w->cwd;

	g_free(panels [idx].last_saved_dir);  /* last path no needed */
        /* Because path can be nonlocal */
	panels [idx].last_saved_dir = vfs_translate_url (widget_work_dir);
    }
}

/* Save current list_view widget directory into panel */
Widget *
restore_into_right_dir_panel (int idx, Widget *from_widget)
{
    Widget *new_widget = NULL;
    const char *saved_dir = panels [idx].last_saved_dir;
    gboolean last_was_panel = (from_widget &&
				get_display_type(idx) != view_listing);
    const char *p_name = get_nth_panel_name (idx);

    if (last_was_panel)
	new_widget = (Widget *) panel_new_with_dir (p_name, saved_dir);
    else
	new_widget = (Widget *) panel_new (p_name);

    return new_widget;
}

/* Return working dir, if it's view_listing - cwd,
   but for other types - last_saved_dir */
const char *
get_panel_dir_for (const WPanel *widget)
{
    int i;

    for (i = 0; i < MAX_VIEWS; i++)
	if ((WPanel *) get_panel_widget (i) == widget)
	    break;

    if (i >= MAX_VIEWS)
	return ".";

    if (get_display_type (i) == view_listing)
	return ((WPanel *) get_panel_widget (i))->cwd;

    return panels[i].last_saved_dir;
}
