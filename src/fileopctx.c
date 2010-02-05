/* File operation contexts for the Midnight Commander

   Copyright (C) 1999, 2001, 2002, 2003, 2004, 2005, 2007
   Free Software Foundation, Inc.

   Authors: Federico Mena <federico@nuclecu.unam.mx>
            Miguel de Icaza <miguel@nuclecu.unam.mx>

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

/** \file fileopctx.c
 *  \brief Source: file operation contexts
 *  \date 1998-2007
 *  \author Federico Mena <federico@nuclecu.unam.mx>
 *  \author Miguel de Icaza <miguel@nuclecu.unam.mx>
 */

#include <config.h>

#include <unistd.h>

#include "lib/global.h"
#include "fileopctx.h"
#include "lib/search.h"
#include "lib/vfs/mc-vfs/vfs.h"

/**
 * \fn FileOpContext * file_op_context_new (FileOperation op)
 * \param op file operation struct
 * \return The newly-created context, filled with the default file mask values.
 *
 * Creates a new file operation context with the default values.  If you later want
 * to have a user interface for this, call file_op_context_create_ui().
 */
FileOpContext *
file_op_context_new (FileOperation op)
{
    FileOpContext *ctx;

    ctx = g_new0 (FileOpContext, 1);
    ctx->operation = op;
    ctx->eta_secs = 0.0;
    ctx->progress_bytes = 0.0;
    ctx->op_preserve = TRUE;
    ctx->do_reget = TRUE;
    ctx->stat_func = mc_lstat;
    ctx->preserve = TRUE;
    ctx->preserve_uidgid = (geteuid () == 0) ? TRUE : FALSE;
    ctx->umask_kill = 0777777;
    ctx->erase_at_end = TRUE;

    return ctx;
}


/**
 * \fn void file_op_context_destroy (FileOpContext *ctx)
 * \param ctx The file operation context to destroy.
 *
 * Destroys the specified file operation context and its associated UI data, if
 * it exists.
 */
void
file_op_context_destroy (FileOpContext *ctx)
{
    g_return_if_fail (ctx != NULL);

    if (ctx->ui)
	file_op_context_destroy_ui (ctx);

    mc_search_free(ctx->search_handle);

    /** \todo FIXME: do we need to free ctx->dest_mask? */

    g_free (ctx);
}
