/* Configure module for the Midnight Commander
   Copyright (C) 1994, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006,
   2007, 2009 Free Software Foundation, Inc. 

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

#include "lib/global.h"
#include "lib/strutil.h"
#include "lib/mcconfig.h"

/*** global variables **************************************************/

extern int utf8_display;

/*** file scope macro definitions **************************************/

/*** file scope type declarations **************************************/

/*** file scope variables **********************************************/

/*** file scope functions **********************************************/

/*** public functions **************************************************/

gchar **
mc_config_get_groups (mc_config_t * mc_config, gsize * len)
{
    gchar **ret;

    if (!mc_config)
    {
	ret = g_try_malloc0 (sizeof (gchar **));
	if (len != NULL)
	    *len=0;
	return ret;
    }
    ret = g_key_file_get_groups (mc_config->handle, len);
    if (ret == NULL)
    {
	ret = g_try_malloc0 (sizeof (gchar **));
    }
    return ret;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

gchar **
mc_config_get_keys (mc_config_t * mc_config, const gchar * group, gsize * len)
{
    gchar **ret;

    if (!mc_config || !group)
    {
	ret = g_try_malloc0 (sizeof (gchar **));
	if (len != NULL)
	    *len=0;
	return ret;
    }
    ret = g_key_file_get_keys (mc_config->handle, group, len, NULL);
    if (ret == NULL)
    {
	ret = g_try_malloc0 (sizeof (gchar **));
    }
    return ret;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

gchar *
mc_config_get_string (mc_config_t * mc_config, const gchar * group,
		      const gchar * param, const gchar * def)
{
    GIConv conv;
    GString *buffer;
    gchar *ret;
    const char *_system_codepage = str_detect_termencoding();

    if (!mc_config || !group || !param)
	return def ? g_strdup (def) : NULL;

    if (! mc_config_has_param(mc_config, group, param))
    {
	mc_config_set_string (mc_config, group, param, def ? def : "");
	return def ? g_strdup (def) : NULL;
    }

    ret = g_key_file_get_string (mc_config->handle, group, param, NULL);

    if (!ret)
	ret = def ? g_strdup (def) : NULL;

    if (str_isutf8 (_system_codepage))
        return ret;

    conv = g_iconv_open (_system_codepage, "UTF-8");
    if (conv == INVALID_CONV)
        return ret;

    buffer = g_string_new ("");

    if (str_convert (conv, ret, buffer) == ESTR_FAILURE)
    {
        g_string_free(buffer, TRUE);
        str_close_conv (conv);
        return ret;
    }
    str_close_conv (conv);

    g_free(ret);

    return g_string_free(buffer, FALSE);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

gchar *
mc_config_get_string_raw (mc_config_t * mc_config, const gchar * group,
		      const gchar * param, const gchar * def)
{
    gchar *ret;

    if (!mc_config || !group || !param)
	return def ? g_strdup (def) : NULL;

    if (! mc_config_has_param(mc_config, group, param))
    {
	mc_config_set_string (mc_config, group, param, def ? def : "");
	return def ? g_strdup (def) : NULL;
    }

    ret = g_key_file_get_string (mc_config->handle, group, param, NULL);

    if (!ret)
	ret = def ? g_strdup (def) : NULL;

    return ret;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

gboolean
mc_config_get_bool (mc_config_t * mc_config, const gchar * group,
		    const gchar * param, gboolean def)
{
    if (!mc_config || !group || !param)
	return def;

    if (! mc_config_has_param(mc_config, group, param))
    {
	mc_config_set_bool (mc_config, group, param, def);
	return def;
    }

    return g_key_file_get_boolean (mc_config->handle, group, param, NULL);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
mc_config_get_int (mc_config_t * mc_config, const gchar * group,
		   const gchar * param, int def)
{
    if (!mc_config || !group || !param)
	return def;

    if (! mc_config_has_param(mc_config, group, param))
    {
	mc_config_set_int (mc_config, group, param, def);
	return def;
    }

    return g_key_file_get_integer (mc_config->handle, group, param, NULL);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

gchar **
mc_config_get_string_list (mc_config_t * mc_config, const gchar * group,
			   const gchar * param, gsize * length)
{
    if (!mc_config || !group || !param)
	return NULL;

    return g_key_file_get_string_list (mc_config->handle, group, param,
				       length, NULL);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

gboolean *
mc_config_get_bool_list (mc_config_t * mc_config, const gchar * group,
			 const gchar * param, gsize * length)
{
    if (!mc_config || !group || !param)
	return NULL;

    return g_key_file_get_boolean_list (mc_config->handle, group, param,
					length, NULL);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int *
mc_config_get_int_list (mc_config_t * mc_config, const gchar * group,
			const gchar * param, gsize * length)
{
    if (!mc_config || !group || !param)
	return NULL;

    return g_key_file_get_integer_list (mc_config->handle, group, param,
					length, NULL);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
