/*
 * ActiveSync client library for email access
 *
 * Copyright © 2011 Intel Corporation.
 *
 * Authors: Mobica Ltd. <www.mobica.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 */

#include <stdio.h>
#include <string.h>

#include "utils.h"

// returns a pointer to the next field. If field is empty will pass back an empty string. Only returns NULL in case of error
gchar*
get_next_field (gchar **data, const gchar *separator)
{
    gchar *result = NULL, *to = *data;
    guint len = 0;  // length of string

    while (*to && (*to != *separator))
    {
        to++;
    }
    len = (to - *data);

    result = (gchar*) g_malloc0 ( (len * sizeof (gchar)) + 1); // allow for null terminate
    if (result)
    {
        strncpy (result, (*data), len);
        result[len] = 0;
        *data += len + 1;
    }

    //g_debug("get_next_field result = %s", result);
    return result;
}


gchar *
strconcatwithseparator (gchar **strings, guint num, const gchar *sep)
{
    gchar *out = NULL;
    guint len = 0;  //length of resulting string in bytes
    int i;

    for (i = 0; i < num; i++)
    {
        if (strings[i])
        {
            len = len + strlen (strings[i]);
        }
        len = len + strlen (sep);
    }

    out = (gchar*) g_malloc0 ( (len * sizeof (gchar)));

    if (out)
    {
        for (i = 0; i < num; i++)
        {
            if (strings[i])
            {
                //g_debug("strcat %s to %s", strings[i], out);
                strcat (out, strings[i]);
            }
            if (i < num - 1)
            {
                strcat (out, sep);
            }
        }
    }

    return out;
}
