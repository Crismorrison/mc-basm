/*
   Provides a log file to ease tracing the program.

   Copyright (C) 2006, 2009 Free Software Foundation, Inc.

   Written: 2006 Roland Illig <roland.illig@gmx.de>.

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

/** \file logging.c
 *  \brief Source: provides a log file to ease tracing the program
 */

#include <config.h>

#include <stdarg.h>
#include <stdio.h>

#include "lib/global.h"
#include "logging.h"
#include "lib/mcconfig.h"
#include "lib/fileloc.h"

#include "src/setup.h"

/*** file scope functions **********************************************/

static gboolean
is_logging_enabled(void)
{
	static gboolean logging_initialized = FALSE;
	static gboolean logging_enabled = FALSE;

	if (!logging_initialized) {
		logging_enabled = mc_config_get_int (mc_main_config,
		        CONFIG_APP_SECTION, "development.enable_logging", FALSE);
		logging_initialized = TRUE;
	}
	return logging_enabled;
}

/*** public functions **************************************************/

void
mc_log(const char *fmt, ...)
{
	va_list args;
	FILE *f;
	char *logfilename;

	if (is_logging_enabled()) {
		va_start(args, fmt);
		logfilename = g_strdup_printf("%s/%s/log", home_dir, MC_USERCONF_DIR);
		if ((f = fopen(logfilename, "a")) != NULL) {
			(void)vfprintf(f, fmt, args);
			(void)fclose(f);
		}
		g_free(logfilename);
		va_end(args);
	}
}
