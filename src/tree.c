/* Directory tree browser for the Midnight Commander
   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2007 Free Software Foundation, Inc.

   Written: 1994, 1996 Janne Kukonlehto
            1997 Norbert Warmuth
            1996, 1999 Miguel de Icaza

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

   This module has been converted to be a widget.

   The program load and saves the tree each time the tree widget is
   created and destroyed.  This is required for the future vfs layer,
   it will be possible to have tree views over virtual file systems.

   */

/** \file tree.c
 *  \brief Source: directory tree browser
 */

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "lib/global.h"

#include "lib/tty/tty.h"
#include "lib/skin.h"
#include "lib/tty/mouse.h"
#include "lib/tty/key.h"
#include "lib/vfs/mc-vfs/vfs.h"
#include "lib/fileloc.h"
#include "lib/strutil.h"

#include "wtools.h"	/* message() */
#include "dir.h"
#include "dialog.h"
#include "widget.h"
#include "panel.h"
#include "main.h"
#include "main-widgets.h"	/* the_menubar */
#include "menu.h"		/* menubar_visible */
#include "file.h"		/* copy_dir_dir(), move_dir_dir(), erase_dir() */
#include "layout.h"		/* command_prompt */
#include "help.h"
#include "treestore.h"
#include "cmd.h"
#include "cmddef.h"
#include "keybind.h"
#include "history.h"
#include "tree.h"

const global_keymap_t *tree_map;

#define tlines(t) (t->is_panel ? t->widget.lines - 2 - (show_mini_info ? 2 : 0) : t->widget.lines)

/* Use the color of the parent widget for the unselected entries */
#define TREE_NORMALC(h) (DLG_NORMALC (h))

/* Specifies the display mode: 1d or 2d */
static gboolean tree_navigation_flag = FALSE;

struct WTree {
    Widget widget;
    struct TreeStore *store;
    tree_entry *selected_ptr;	/* The selected directory */
    char search_buffer[256];	/* Current search string */
    tree_entry **tree_shown;	/* Entries currently on screen */
    int is_panel;		/* panel or plain widget flag */
    int active;			/* if it's currently selected */
    int searching;		/* Are we on searching mode? */
    int topdiff;		/* The difference between the topmost
				   shown and the selected */
};

/* Forwards */
static void tree_rescan (void *data);

static tree_entry *
back_ptr (tree_entry *ptr, int *count)
{
    int i = 0;

    while (ptr && ptr->prev && i < *count){
	ptr = ptr->prev;
	i ++;
    }
    *count = i;
    return ptr;
}

static tree_entry *
forw_ptr (tree_entry *ptr, int *count)
{
    int i = 0;

    while (ptr && ptr->next && i < *count){
	ptr = ptr->next;
	i ++;
    }
    *count = i;
    return ptr;
}

static void
remove_callback (tree_entry *entry, void *data)
{
    WTree *tree = data;

    if (tree->selected_ptr == entry){
	if (tree->selected_ptr->next)
	    tree->selected_ptr = tree->selected_ptr->next;
	else
	    tree->selected_ptr = tree->selected_ptr->prev;
    }
}

/* Save the ~/.mc/Tree file */
static void
save_tree (WTree *tree)
{
    int error;
    char *tree_name;

    (void) tree;
    error = tree_store_save ();


    if (error){
	tree_name = g_build_filename (home_dir, MC_USERCONF_DIR,
					MC_TREESTORE_FILE, (char *) NULL);
	fprintf (stderr, _("Cannot open the %s file for writing:\n%s\n"), tree_name,
		unix_error_string (error));
	g_free (tree_name);
    }
}

static void
tree_remove_entry (WTree *tree, char *name)
{
    (void) tree;
    tree_store_remove_entry (name);
}

static void
tree_destroy (WTree *tree)
{
    tree_store_remove_entry_remove_hook (remove_callback);
    save_tree (tree);

    g_free (tree->tree_shown);
    tree->tree_shown = 0;
    tree->selected_ptr = NULL;
}

/* Loads the .mc.tree file */
static void
load_tree (WTree *tree)
{
    tree_store_load ();

    tree->selected_ptr = tree->store->tree_first;
    tree_chdir (tree, home_dir);
}

static void
tree_show_mini_info (WTree *tree, int tree_lines, int tree_cols)
{
    Dlg_head *h = tree->widget.parent;
    int      line;

    /* Show mini info */
    if (tree->is_panel){
	if (!show_mini_info)
	    return;
	line = tree_lines+2;
    } else
	line = tree_lines+1;

    tty_draw_hline (tree->widget.y + line, tree->widget.x + 1, ' ', tree_cols);
    widget_move (&tree->widget, line, 1);

    if (tree->searching){
	/* Show search string */
	tty_setcolor (TREE_NORMALC (h));
	tty_setcolor (DLG_FOCUSC (h));
	tty_print_char (PATH_SEP);

	tty_print_string (str_fit_to_term (tree->search_buffer, 
		tree_cols - 2, J_LEFT_FIT));
	tty_print_char (' ');
	tty_setcolor (DLG_FOCUSC (h));
    } else {
	/* Show full name of selected directory */
	tty_print_string (str_fit_to_term (tree->selected_ptr->name, 
		tree_cols, J_LEFT_FIT));
    }
}

static void
show_tree (WTree *tree)
{
    Dlg_head *h = tree->widget.parent;
    tree_entry *current;
    int i, j, topsublevel;
    int x, y;
    int tree_lines, tree_cols;

    /* Initialize */
    x = y = 0;
    tree_lines = tlines (tree);
    tree_cols  = tree->widget.cols;

    tty_setcolor (TREE_NORMALC (h));
    widget_move ((Widget*)tree, y, x);
    if (tree->is_panel){
	tree_cols  -= 2;
	x = y = 1;
    }

    g_free (tree->tree_shown);
    tree->tree_shown = g_new0 (tree_entry *, tree_lines);

    if (tree->store->tree_first)
	topsublevel = tree->store->tree_first->sublevel;
    else
	topsublevel = 0;
    if (!tree->selected_ptr){
	tree->selected_ptr = tree->store->tree_first;
	tree->topdiff = 0;
    }
    current = tree->selected_ptr;

    /* Calculate the directory which is to be shown on the topmost line */
    if (!tree_navigation_flag)
	current = back_ptr (current, &tree->topdiff);
    else {
	i = 0;
	while (current->prev && i < tree->topdiff){
	    current = current->prev;
	    if (current->sublevel < tree->selected_ptr->sublevel){
		if (strncmp (current->name, tree->selected_ptr->name,
			     strlen (current->name)) == 0)
		    i++;
	    } else if (current->sublevel == tree->selected_ptr->sublevel){
		for (j = strlen (current->name) - 1; current->name [j] != PATH_SEP; j--);
		if (strncmp (current->name, tree->selected_ptr->name, j) == 0)
		    i++;
	    } else if (current->sublevel == tree->selected_ptr->sublevel + 1
		       && strlen (tree->selected_ptr->name) > 1){
		if (strncmp (current->name, tree->selected_ptr->name,
			     strlen (tree->selected_ptr->name)) == 0)
		    i++;
	    }
	}
	tree->topdiff = i;
    }

    /* Loop for every line */
    for (i = 0; i < tree_lines; i++){
	/* Move to the beginning of the line */
	tty_draw_hline (tree->widget.y + y + i, tree->widget.x + x, ' ', tree_cols);

	if (!current)
	    continue;

	tree->tree_shown [i] = current;
	if (current->sublevel == topsublevel){

	    /* Top level directory */
	    if (tree->active && current == tree->selected_ptr) {
		if (!tty_use_colors () && !tree->is_panel)
			tty_setcolor (MARKED_COLOR);
		else
			tty_setcolor (SELECTED_COLOR);
	    }

	    /* Show full name */
	    tty_print_string (str_fit_to_term (current->name, tree_cols - 6, J_LEFT_FIT));
	} else{
	    /* Sub level directory */

	    tty_set_alt_charset (TRUE);
	    /* Output branch parts */
	    for (j = 0; j < current->sublevel - topsublevel - 1; j++){
		if (tree_cols - 8 - 3 * j < 9)
		    break;
		tty_print_char (' ');
		if (current->submask & (1 << (j + topsublevel + 1)))
		    tty_print_char (ACS_VLINE);
		else
		    tty_print_char (' ');
		tty_print_char (' ');
	    }
	    tty_print_char (' '); j++;
	    if (!current->next || !(current->next->submask & (1 << current->sublevel)))
		tty_print_char (ACS_LLCORNER);
	    else
		tty_print_char (ACS_LTEE);
	    tty_print_char (ACS_HLINE);
	    tty_set_alt_charset (FALSE);

	    if (tree->active && current == tree->selected_ptr) {
		/* Selected directory -> change color */
		if (!tty_use_colors () && !tree->is_panel)
		    tty_setcolor (MARKED_COLOR);
		else
		    tty_setcolor (SELECTED_COLOR);
	    }

	    /* Show sub-name */
	    tty_print_char (' ');
	    tty_print_string (str_fit_to_term (current->subname, 
		    tree_cols - 2 - 4 - 3 * j, J_LEFT_FIT));
	}
	tty_print_char (' ');

	/* Return to normal color */
	tty_setcolor (TREE_NORMALC (h));

	/* Calculate the next value for current */
	current = current->next;
	if (tree_navigation_flag){
	    while (current){
		if (current->sublevel < tree->selected_ptr->sublevel){
		    if (strncmp (current->name, tree->selected_ptr->name,
				 strlen (current->name)) == 0)
			break;
		} else if (current->sublevel == tree->selected_ptr->sublevel){
		    for (j = strlen (current->name) - 1; current->name [j] != PATH_SEP; j--);
		    if (strncmp (current->name,tree->selected_ptr->name,j)== 0)
			break;
		} else if (current->sublevel == tree->selected_ptr->sublevel+1
			   && strlen (tree->selected_ptr->name) > 1){
		    if (strncmp (current->name, tree->selected_ptr->name,
				 strlen (tree->selected_ptr->name)) == 0)
			break;
		}
		current = current->next;
	    }
	}
    }
    tree_show_mini_info (tree, tree_lines, tree_cols);
}

static void
tree_check_focus (WTree *tree)
{
    if (tree->topdiff < 3)
	tree->topdiff = 3;
    else if (tree->topdiff >= tlines (tree) - 3)
	tree->topdiff = tlines (tree) - 3 - 1;
}

static void
tree_move_backward (WTree *tree, int i)
{
    if (!tree_navigation_flag)
	tree->selected_ptr = back_ptr (tree->selected_ptr, &i);
    else {
	tree_entry *current;
	int j = 0;

	current = tree->selected_ptr;
	while (j < i && current->prev
	       && current->prev->sublevel >= tree->selected_ptr->sublevel){
	    current = current->prev;
	    if (current->sublevel == tree->selected_ptr->sublevel){
		tree->selected_ptr = current;
		j++;
	    }
	}
	i = j;
    }

    tree->topdiff -= i;
    tree_check_focus (tree);
}

static void
tree_move_forward (WTree *tree, int i)
{
    if (!tree_navigation_flag)
	tree->selected_ptr = forw_ptr (tree->selected_ptr, &i);
    else {
	tree_entry *current;
	int j = 0;

	current = tree->selected_ptr;
	while (j < i && current->next
	       && current->next->sublevel >= tree->selected_ptr->sublevel){
	    current = current->next;
	    if (current->sublevel == tree->selected_ptr->sublevel){
		tree->selected_ptr = current;
		j ++;
	    }
	}
	i = j;
    }

    tree->topdiff += i;
    tree_check_focus (tree);
}

static void
tree_move_to_child (WTree *tree)
{
    tree_entry *current;

    /* Do we have a starting point? */
    if (!tree->selected_ptr)
	return;
    /* Take the next entry */
    current = tree->selected_ptr->next;
    /* Is it the child of the selected entry */
    if (current && current->sublevel > tree->selected_ptr->sublevel){
	/* Yes -> select this entry */
	tree->selected_ptr = current;
	tree->topdiff++;
	tree_check_focus (tree);
    } else {
	/* No -> rescan and try again */
	tree_rescan (tree);
	current = tree->selected_ptr->next;
	if (current && current->sublevel > tree->selected_ptr->sublevel){
	    tree->selected_ptr = current;
	    tree->topdiff++;
	    tree_check_focus (tree);
	}
    }
}

static gboolean
tree_move_to_parent (WTree *tree)
{
    tree_entry *current;
    tree_entry *old;

    if (!tree->selected_ptr)
	return FALSE;

    old = tree->selected_ptr;
    current = tree->selected_ptr->prev;
    while (current && current->sublevel >= tree->selected_ptr->sublevel){
	current = current->prev;
	tree->topdiff--;
    }
    if (!current)
	current = tree->store->tree_first;
    tree->selected_ptr = current;
    tree_check_focus (tree);
    return tree->selected_ptr != old;
}

static void
tree_move_to_top (WTree *tree)
{
    tree->selected_ptr = tree->store->tree_first;
    tree->topdiff = 0;
}

static void
tree_move_to_bottom (WTree *tree)
{
    tree->selected_ptr = tree->store->tree_last;
    tree->topdiff = tlines (tree) - 3 - 1;
}

/* Handle mouse click */
static void
tree_event (WTree *tree, int y)
{
    if (tree->tree_shown [y]){
	tree->selected_ptr = tree->tree_shown [y];
	tree->topdiff = y;
    }
    show_tree (tree);
}

static void
tree_chdir_sel (WTree *tree)
{
    if (!tree->is_panel)
	return;

    change_panel ();

    if (do_cd (tree->selected_ptr->name, cd_exact))
	select_item (current_panel);
    else
	message (D_ERROR, MSG_ERROR, _(" Cannot chdir to \"%s\" \n %s "),
		 tree->selected_ptr->name, unix_error_string (errno));

    change_panel ();
    show_tree (tree);
}

static void
maybe_chdir (WTree *tree)
{
    if (xtree_mode && tree->is_panel && is_idle ())
	tree_chdir_sel (tree);
}

/* Mouse callback */
static int
event_callback (Gpm_Event *event, void *data)
{
    WTree *tree = data;

    /* rest of the upper frame, the menu is invisible - call menu */
    if (tree->is_panel && (event->type & GPM_DOWN)
	    && event->y == 1 && !menubar_visible) {
	event->x += tree->widget.x;
	return the_menubar->widget.mouse (event, the_menubar);
    }

    if (!(event->type & GPM_UP))
	return MOU_NORMAL;

    if (tree->is_panel)
	event->y--;

    event->y--;

    if (!tree->active)
	change_panel ();

    if (event->y < 0){
	tree_move_backward (tree, tlines (tree) - 1);
	show_tree (tree);
    } else if (event->y >= tlines (tree)){
	tree_move_forward (tree, tlines (tree) - 1);
	show_tree (tree);
    } else {
	tree_event (tree, event->y);
	if ((event->type & (GPM_UP|GPM_DOUBLE)) == (GPM_UP|GPM_DOUBLE)){
	    tree_chdir_sel (tree);
	}
    }
    return MOU_NORMAL;
}

/* Search tree for text */
static int
search_tree (WTree *tree, char *text)
{
    tree_entry *current;
    int len;
    int wrapped = 0;
    int found = 0;

    len = strlen (text);
    current = tree->selected_ptr;
    found = 0;
    while (!wrapped || current != tree->selected_ptr){
	if (strncmp (current->subname, text, len) == 0){
	    tree->selected_ptr = current;
	    found = 1;
	    break;
	}
	current = current->next;
	if (!current){
	    current = tree->store->tree_first;
	    wrapped = 1;
	}
	tree->topdiff++;
    }
    tree_check_focus (tree);
    return found;
}

static void
tree_do_search (WTree *tree, int key)
{
    size_t l;

    l = strlen (tree->search_buffer);
    if ((l != 0) && (key == KEY_BACKSPACE))
	tree->search_buffer [--l] = '\0';
    else if (key && l < sizeof (tree->search_buffer)){
	tree->search_buffer [l] = key;
	tree->search_buffer [++l] = '\0';
    }

    if (!search_tree (tree, tree->search_buffer))
	tree->search_buffer [--l] = 0;

    show_tree (tree);
    maybe_chdir (tree);
}

static void
tree_rescan (void *data)
{
    char old_dir [MC_MAXPATHLEN];
    WTree *tree = data;

    if (!tree->selected_ptr || !mc_get_current_wd (old_dir, MC_MAXPATHLEN) ||
	mc_chdir (tree->selected_ptr->name))
	return;

    tree_store_rescan (tree->selected_ptr->name);
    mc_chdir (old_dir);
}

static void
tree_forget (void *data)
{
    WTree *tree = data;
    if (tree->selected_ptr)
	tree_remove_entry (tree, tree->selected_ptr->name);
}

static void
tree_copy (WTree *tree, const char *default_dest)
{
    char   msg [BUF_MEDIUM];
    char   *dest;
    off_t  count = 0;
    double bytes = 0;
    FileOpContext *ctx;

    if (tree->selected_ptr == NULL)
	return;

    g_snprintf (msg, sizeof (msg), _("Copy \"%s\" directory to:"),
			str_trunc (tree->selected_ptr->name, 50));
    dest = input_expand_dialog (Q_("DialogTitle|Copy"), msg, MC_HISTORY_FM_TREE_COPY, default_dest);

    if (dest != NULL && *dest != '\0') {
	ctx = file_op_context_new (OP_COPY);
	file_op_context_create_ui (ctx, FALSE);
	copy_dir_dir (ctx, tree->selected_ptr->name, dest, 1, 0, 0, 0, &count, &bytes);
	file_op_context_destroy (ctx);
    }

    g_free (dest);
}

static void
tree_move (WTree *tree, const char *default_dest)
{
    char   msg [BUF_MEDIUM];
    char   *dest;
    struct stat buf;
    double bytes = 0;
    off_t  count = 0;
    FileOpContext *ctx;

    if (tree->selected_ptr == NULL)
	return;

    g_snprintf (msg, sizeof (msg), _("Move \"%s\" directory to:"),
			str_trunc (tree->selected_ptr->name, 50));
    dest = input_expand_dialog (Q_("DialogTitle|Move"), msg, MC_HISTORY_FM_TREE_MOVE, default_dest);

    if (dest == NULL || *dest == '\0') {
	g_free (dest);
	return;
    }

    if (stat (dest, &buf)){
	message (D_ERROR, MSG_ERROR, _(" Cannot stat the destination \n %s "),
		 unix_error_string (errno));
	g_free (dest);
	return;
    }

    if (!S_ISDIR (buf.st_mode)){
	file_error (_(" Destination \"%s\" must be a directory \n %s "),
		    dest);
	g_free (dest);
	return;
    }

    ctx = file_op_context_new (OP_MOVE);
    file_op_context_create_ui (ctx, FALSE);
    move_dir_dir (ctx, tree->selected_ptr->name, dest, &count, &bytes);
    file_op_context_destroy (ctx);

    g_free (dest);
}

#if 0
static void
tree_mkdir (WTree *tree)
{
    char old_dir [MC_MAXPATHLEN];

    if (!tree->selected_ptr)
	return;
    if (!mc_get_current_wd (old_dir, MC_MAXPATHLEN))
	return;
    if (chdir (tree->selected_ptr->name))
	return;
    /* FIXME
    mkdir_cmd (tree);
    */
    tree_rescan (tree);
    chdir (old_dir);
}
#endif

static void
tree_rmdir (void *data)
{
    WTree *tree = data;
    off_t count = 0;
    double bytes = 0;
    FileOpContext *ctx;

    if (!tree->selected_ptr)
	return;

    if (confirm_delete) {
	char *buf;
	int result;

	buf =
	    g_strdup_printf (_("  Delete %s?  "),
			     tree->selected_ptr->name);
	result =
	    query_dialog (Q_("DialogTitle|Delete"), buf, D_ERROR, 2, _("&Yes"), _("&No"));
	g_free (buf);
	if (result != 0)
	    return;
    }

    ctx = file_op_context_new (OP_DELETE);
    file_op_context_create_ui (ctx, FALSE);
    if (erase_dir (ctx, tree->selected_ptr->name, &count, &bytes) == FILE_CONT)
	tree_forget (tree);
    file_op_context_destroy (ctx);
}

static inline void
tree_move_up (WTree *tree)
{
    tree_move_backward (tree, 1);
    show_tree (tree);
    maybe_chdir (tree);
}

static inline void
tree_move_down (WTree *tree)
{
    tree_move_forward (tree, 1);
    show_tree (tree);
    maybe_chdir (tree);
}

static inline void
tree_move_home (WTree *tree)
{
    tree_move_to_top (tree);
    show_tree (tree);
    maybe_chdir (tree);
}

static inline void
tree_move_end (WTree *tree)
{
    tree_move_to_bottom (tree);
    show_tree (tree);
    maybe_chdir (tree);
}

static void
tree_move_pgup (WTree *tree)
{
    tree_move_backward (tree, tlines (tree) - 1);
    show_tree (tree);
    maybe_chdir (tree);
}

static void
tree_move_pgdn (WTree *tree)
{
    tree_move_forward (tree, tlines (tree) - 1);
    show_tree (tree);
    maybe_chdir (tree);
}

static gboolean
tree_move_left (WTree *tree)
{
    gboolean v = FALSE;

    if (tree_navigation_flag) {
	v = tree_move_to_parent (tree);
	show_tree (tree);
	maybe_chdir (tree);
    }

    return v;
}

static gboolean
tree_move_right (WTree *tree)
{
    gboolean v = FALSE;

    if (tree_navigation_flag) {
	tree_move_to_child (tree);
	show_tree (tree);
	maybe_chdir (tree);
	v = TRUE;
    }

    return v;
}

static void
tree_start_search (WTree *tree)
{
    gboolean i;

    if (tree->searching) {
	if (tree->selected_ptr == tree->store->tree_last)
	    tree_move_to_top (tree);
	else {
	/* set navigation mode temporarily to 'Static' because in
	 * dynamic navigation mode tree_move_forward will not move
	 * to a lower sublevel if necessary (sequent searches must
	 * start with the directory followed the last found directory)
         */
	    i = tree_navigation_flag;
	    tree_navigation_flag = 0;
	    tree_move_forward (tree, 1);
	    tree_navigation_flag = i;
	}
	tree_do_search (tree, 0);
    } else {
	tree->searching = 1;
	tree->search_buffer[0] = 0;
    }
}

static void
tree_toggle_navig (WTree *tree)
{
    tree_navigation_flag = !tree_navigation_flag;
    buttonbar_set_label (find_buttonbar (tree->widget.parent), 4,
			tree_navigation_flag ? Q_("ButtonBar|Static")
						: Q_("ButtonBar|Dynamc"),
			tree_map, (Widget *) tree);
}

static cb_ret_t
tree_execute_cmd (WTree *tree, unsigned long command)
{
    cb_ret_t res = MSG_HANDLED;

    if (command != CK_TreeStartSearch)
	tree->searching = 0;

    switch (command) {
    case CK_TreeHelp:
	interactive_display (NULL, "[Directory Tree]");
	break;
    case CK_TreeForget:
	tree_forget (tree);
	break;
    case CK_TreeToggleNav:
	tree_toggle_navig (tree);
	break;
    case CK_TreeCopy:
	tree_copy (tree, "");
	break;
    case CK_TreeMove:
	tree_move (tree, "");
	break;
    case CK_TreeMoveUp:
	tree_move_up (tree);
	break;
    case CK_TreeMoveDown:
	tree_move_down (tree);
	break;
    case CK_TreeMoveHome:
	tree_move_home (tree);
	break;
    case CK_TreeMoveEnd:
	tree_move_end (tree);
	break;
    case CK_TreeMovePgUp:
	tree_move_pgup (tree);
	break;
    case CK_TreeMovePgDn:
	tree_move_pgdn (tree);
	break;
    case CK_TreeOpen:
	tree_chdir_sel (tree);
	break;
    case CK_TreeRescan:
	tree_rescan (tree);
	break;
    case CK_TreeStartSearch:
	tree_start_search (tree);
	break;
    case CK_TreeRemove:
	tree_rmdir (tree);
	break;
    default:
        res = MSG_NOT_HANDLED;
    }

    show_tree (tree);

    return res;
}

static cb_ret_t
tree_key (WTree *tree, int key)
{
    size_t i;

    for (i = 0; tree_map [i].key != 0; i++)
	if (key == tree_map [i].key)
	    switch (tree_map [i].command) {
	    case CK_TreeMoveLeft:
		return tree_move_left (tree) ? MSG_HANDLED : MSG_NOT_HANDLED;
	    case CK_TreeMoveRight:
		return tree_move_right (tree) ? MSG_HANDLED : MSG_NOT_HANDLED;
	    default:
		tree_execute_cmd (tree, tree_map [i].command);
		return MSG_HANDLED;
	    }

    if (is_abort_char (key)) {
	if (tree->is_panel) {
	    tree->searching = 0;
	    show_tree (tree);
	    return MSG_HANDLED;  /* eat abort char */
	}
	/* modal tree dialog: let upper layer see the
	   abort character and close the dialog */
	return MSG_NOT_HANDLED;
    }

    /* Do not eat characters not meant for the tree below ' ' (e.g. C-l). */
    if ((key >= ' ' && key <= 255) || key == KEY_BACKSPACE) {
	if (tree->searching){
	    tree_do_search (tree, key);
	    show_tree (tree);
	    return MSG_HANDLED;
	}

	if (!command_prompt) {
	    tree_start_search (tree);
	    tree_do_search (tree, key);
	    return MSG_HANDLED;
	}
	return tree->is_panel ? MSG_HANDLED : MSG_NOT_HANDLED;
    }

    return MSG_NOT_HANDLED;
}

static void
tree_frame (Dlg_head *h, WTree *tree)
{
    tty_setcolor (NORMAL_COLOR);
    widget_erase ((Widget*) tree);
    if (tree->is_panel) {
	draw_box (h, tree->widget.y, tree->widget.x, tree->widget.lines,
		     tree->widget.cols);

	if (show_mini_info)
	    tty_draw_hline (tree->widget.y + tlines (tree) + 1,
			    tree->widget.x + 1,
			    ACS_HLINE, tree->widget.cols - 2);
    }
}

static cb_ret_t
tree_callback (Widget *w, widget_msg_t msg, int parm)
{
    WTree *tree = (WTree *) w;
    Dlg_head *h = tree->widget.parent;
    WButtonBar *b = find_buttonbar (h);

    switch (msg) {
    case WIDGET_DRAW:
	tree_frame (h, tree);
	show_tree (tree);
	return MSG_HANDLED;

    case WIDGET_FOCUS:
	tree->active = 1;
	buttonbar_set_label (b, 1, Q_("ButtonBar|Help"), tree_map, (Widget *) tree);
	buttonbar_set_label (b, 2, Q_("ButtonBar|Rescan"), tree_map, (Widget *) tree);
	buttonbar_set_label (b, 3, Q_("ButtonBar|Forget"), tree_map, (Widget *) tree);
	buttonbar_set_label (b, 4, tree_navigation_flag ? Q_("ButtonBar|Static")
								: Q_("ButtonBar|Dynamc"),
			    tree_map, (Widget *) tree);
	buttonbar_set_label (b, 5, Q_("ButtonBar|Copy"), tree_map, (Widget *) tree);
	buttonbar_set_label (b, 6, Q_("ButtonBar|RenMov"), tree_map, (Widget *) tree);
#if 0
	/* FIXME: mkdir is currently defunct */
	buttonbar_set_label (b, 7, Q_("ButtonBar|Mkdir"), tree_map, (Widget *) tree);
#else
	buttonbar_clear_label (b, 7, (Widget *) tree);
#endif
	buttonbar_set_label (b, 8, Q_("ButtonBar|Rmdir"), tree_map, (Widget *) tree);
	buttonbar_redraw (b);

	/* FIXME: Should find a better way of only displaying the
	   currently selected item */
	show_tree (tree);
	return MSG_HANDLED;

	/* FIXME: Should find a better way of changing the color of the
	   selected item */

    case WIDGET_UNFOCUS:
	tree->active = 0;
	show_tree (tree);
	return MSG_HANDLED;

    case WIDGET_KEY:
	return tree_key (tree, parm);

    case WIDGET_COMMAND:
	/* command from buttonbar */
	return tree_execute_cmd (tree, parm);

    case WIDGET_DESTROY:
	tree_destroy (tree);
	return MSG_HANDLED;

    default:
	return default_proc (msg, parm);
    }
}

WTree *
tree_new (int is_panel, int y, int x, int lines, int cols)
{
    WTree *tree = g_new (WTree, 1);

    init_widget (&tree->widget, y, x, lines, cols,
		 tree_callback, event_callback);
    tree->is_panel = is_panel;
    tree->selected_ptr = 0;

    tree->store = tree_store_get ();
    tree_store_add_entry_remove_hook (remove_callback, tree);
    tree->tree_shown = 0;
    tree->search_buffer[0] = 0;
    tree->topdiff = tree->widget.lines / 2;
    tree->searching = 0;
    tree->active = 0;

    /* We do not want to keep the cursor */
    widget_want_cursor (tree->widget, 0);
    load_tree (tree);
    return tree;
}

void
tree_chdir (WTree *tree, const char *dir)
{
    tree_entry *current;

    current = tree_store_whereis (dir);

    if (current != NULL) {
	tree->selected_ptr = current;
	tree_check_focus (tree);
    }
}

/* Return name of the currently selected entry */
char *
tree_selected_name (const WTree *tree)
{
    return tree->selected_ptr->name;
}

void
sync_tree (const char *path)
{
    tree_chdir (the_tree, path);
}

WTree *
find_tree (struct Dlg_head *h)
{
    return (WTree *) find_widget_type (h, tree_callback);
}
