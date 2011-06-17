/**
 *
 *  Filename: libeassync.c
 *  Project:
 *  Description: client side library for eas calendar (wraps dbus api)
 *
 */

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlsave.h>
#include <libedataserver/e-flag.h>

#include "../../eas-daemon/src/activesyncd-common-defs.h"
#include "libeassync.h"
#include "eas-cal-info.h"

#include "../../logger/eas-logger.h"

G_DEFINE_TYPE (EasSyncHandler, eas_sync_handler, G_TYPE_OBJECT);

struct _EasSyncHandlerPrivate
{
    DBusGConnection* bus;
    DBusGProxy *remoteEas;
    gchar* account_uid;     // TODO - is it appropriate to have a dbus proxy per account if we have multiple accounts making requests at same time?
    GMainLoop* main_loop;

};

static gboolean
build_serialised_calendar_info_array (gchar ***serialised_cal_info_array, const GSList *cal_list, GError **error);

// TODO - how much verification of args should happen

static void
eas_sync_handler_init (EasSyncHandler *cnc)
{
    EasSyncHandlerPrivate *priv = NULL;
    g_debug ("eas_sync_handler_init++");

    /* allocate internal structure */
    priv = g_new0 (EasSyncHandlerPrivate, 1);

    priv->remoteEas = NULL;
    priv->bus = NULL;
    priv->account_uid = NULL;
    priv->main_loop = NULL;
    cnc->priv = priv;
    g_debug ("eas_sync_handler_init--");
}

static void
eas_sync_handler_finalize (GObject *object)
{
    EasSyncHandler *cnc = (EasSyncHandler *) object;
    EasSyncHandlerPrivate *priv;

    g_debug ("eas_sync_handler_finalize++");

    priv = cnc->priv;
    g_free (priv->account_uid);

    g_main_loop_quit (priv->main_loop);
    dbus_g_connection_unref (priv->bus);
    g_free (priv);
    cnc->priv = NULL;

    G_OBJECT_CLASS (eas_sync_handler_parent_class)->finalize (object);
    g_debug ("eas_sync_handler_finalize--");
}

static void
eas_sync_handler_class_init (EasSyncHandlerClass *klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS (klass);
    GObjectClass* parent_class = G_OBJECT_CLASS (klass);
    void *temp = (void*) object_class;
    temp = (void*) parent_class;

    g_debug ("eas_sync_handler_class_init++");

    object_class->finalize = eas_sync_handler_finalize;
    g_debug ("eas_sync_handler_class_init--");
}

EasSyncHandler *
eas_sync_handler_new (const gchar* account_uid)
{
    GError* error = NULL;
    EasSyncHandler *object = NULL;

    g_type_init();

    g_log_set_default_handler (eas_logger, NULL);
    g_debug ("eas_sync_handler_new++ : account_uid[%s]",
             (account_uid ? account_uid : "NULL"));

    if (!account_uid) return NULL;

    object = g_object_new (EAS_TYPE_SYNC_HANDLER , NULL);

    if (object == NULL)
    {
        g_error ("Error: Couldn't create sync handler");
        return NULL;
    }

    object->priv->main_loop = g_main_loop_new (NULL, TRUE);

    if (object->priv->main_loop == NULL)
    {
        g_error ("Error: Failed to create the mainloop");
        return NULL;
    }

    g_debug ("Connecting to Session D-Bus.");
    object->priv->bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
    if (error != NULL)
    {
        g_error ("Error: Couldn't connect to the Session bus (%s) ", error->message);
        return NULL;
    }

    g_debug ("Creating a GLib proxy object for Eas.");
    object->priv->remoteEas =  dbus_g_proxy_new_for_name (object->priv->bus,
                                                          EAS_SERVICE_NAME,
                                                          EAS_SERVICE_SYNC_OBJECT_PATH,
                                                          EAS_SERVICE_SYNC_INTERFACE);
    if (object->priv->remoteEas == NULL)
    {
        g_error ("Error: Couldn't create the proxy object");
        return NULL;
    }

    object->priv->account_uid = g_strdup (account_uid);

    g_debug ("eas_sync_handler_new--");
    return object;

}

static void
free_string_array (gchar **array)
{
    guint i = 0;
    while (array[i])
    {
        g_free (array[i]);
        i++;
    }
    g_free (array);

}

gboolean eas_sync_handler_get_items (EasSyncHandler* self,
                                              const gchar *sync_key_in,
                                              gchar **sync_key_out,
                                              EasItemType type,
                                              const gchar* folder_id,
                                              GSList **items_created,
                                              GSList **items_updated,
                                              GSList **items_deleted,
                                              GError **error)
{
    gboolean ret = TRUE;
    DBusGProxy *proxy = self->priv->remoteEas;
    gchar **created_item_array = NULL;
    gchar **deleted_item_array = NULL;
    gchar **updated_item_array = NULL;
	gchar *folder = NULL;

    g_debug ("eas_sync_handler_get_calendar_items++");

	//if sync_key not set - assign to 0 to re-initiate sync relationship
	if(sync_key_in ==NULL)
	{
		sync_key_in = g_strdup("0");
	}

	if(folder_id ==NULL)
	{
		//If folder_id is not set, then we set it to the default for the type
		switch(type)
		{
			case EAS_ITEM_CALENDAR:
			{
				folder = g_strdup("1");
			}
			break;
			default:
			{
				//unknown type -no default folder, set GError & return_type
			}
		}
	}
	else //use passed in folder_id
	{
		folder = g_strdup(folder_id);
	}

    g_debug ("eas_sync_handler_get_latest_items - dbus proxy ok");

    g_assert (g_slist_length (*items_created) == 0);
    g_assert (g_slist_length (*items_updated) == 0);
    g_assert (g_slist_length (*items_deleted) == 0);

    // call DBus API
    ret = dbus_g_proxy_call (proxy, "get_latest_items", error,
                             G_TYPE_STRING, self->priv->account_uid,
                             G_TYPE_UINT64, (guint64) type,
                             G_TYPE_STRING, folder,
                             G_TYPE_STRING, sync_key_in,
                             G_TYPE_INVALID,
                             G_TYPE_STRING, sync_key_out,
                             G_TYPE_STRV, &created_item_array,
                             G_TYPE_STRV, &deleted_item_array,
                             G_TYPE_STRV, &updated_item_array,
                             G_TYPE_INVALID);

    g_debug ("eas_sync_handler_get_latest_items - dbus proxy called");

	g_free(folder);

	if (*error)
    {
        g_error (" Error: %s", (*error)->message);
    }

    if (ret)
    {
        guint i = 0;
        g_debug ("get_latest_calendar_items called successfully");
        while (created_item_array[i])
        {
            EasCalInfo *cal = eas_cal_info_new ();
            g_debug ("created item = %s", created_item_array[i]);
            eas_cal_info_deserialise (cal, created_item_array[i]);
            g_debug ("created item server id = %s", cal->server_id);
            *items_created = g_slist_append (*items_created, cal);
            i++;
        }
        i = 0;
        while (updated_item_array[i])
        {
            EasCalInfo *cal = eas_cal_info_new ();
            g_debug ("created item = %s", updated_item_array[i]);
            eas_cal_info_deserialise (cal, updated_item_array[i]);
            g_debug ("created item server id = %s", cal->server_id);
            *items_updated = g_slist_append (*items_updated, cal);
            i++;
        }
        i = 0;
        while (deleted_item_array[i])
        {
            g_debug ("deleted item = %s", deleted_item_array[i]);
            *items_deleted = g_slist_append (*items_deleted, deleted_item_array[i]);
            i++;
        }

    }

    free_string_array (created_item_array);
    free_string_array (updated_item_array);
    free_string_array (deleted_item_array);

    if (!ret)   // failed - cleanup lists
    {
        g_slist_foreach (*items_created, (GFunc) g_free, NULL);
        g_free (*items_created);
        *items_created = NULL;
        g_slist_foreach (*items_updated, (GFunc) g_free, NULL);
        g_free (*items_updated);
        *items_updated = NULL;
        g_slist_foreach (*items_deleted, (GFunc) g_free, NULL);
        g_free (*items_deleted);
        *items_deleted = NULL;
    }

    g_debug ("eas_sync_handler_get_items--");
    return ret;
}

gboolean
eas_sync_handler_delete_items (EasSyncHandler* self,
                               const gchar *sync_key_in,
                               gchar **sync_key_out,
                               EasItemType type,
                               const gchar* folder_id,
                               GSList *items_deleted,
                               GError **error)
{
    gboolean ret = TRUE;
    DBusGProxy *proxy = self->priv->remoteEas;
	gchar *folder = NULL;
	
    g_debug ("eas_sync_handler_delete_items++");

	if(sync_key_in ==NULL || !g_strcmp0 (sync_key_in, "0"))
	{
		g_set_error (error, EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_BADARG,
                     ("delete_items requires a valid sync key"));
		return FALSE;
	}
	if(folder_id ==NULL)
	{
		//If folder_id is not set, then we set it to the default for the type
		switch(type)
		{
			case EAS_ITEM_CALENDAR:
			{
				folder = g_strdup("1");
			}
			break;
			default:
			{
				//unknown type -no default folder, set GError & return_type
			}
		}
	}
	else //use passed in folder_id
	{
		folder = g_strdup(folder_id);
	}


    g_debug ("eas_sync_handler_delete_items - dbus proxy ok");

    // call DBus API
    ret = dbus_g_proxy_call (proxy, "delete_items", error,
                             G_TYPE_STRING, self->priv->account_uid,
                             G_TYPE_STRING, folder_id,
                             G_TYPE_STRING, sync_key_in,
                             G_TYPE_STRV, items_deleted,
                             G_TYPE_INVALID,
                             G_TYPE_STRING, sync_key_out,
                             G_TYPE_INVALID);

    g_debug ("eas_sync_handler_delete_items - dbus proxy called");
	g_free(folder);
	
    if (*error)
    {
        g_error (" Error: %s", (*error)->message);
    }

    if (ret)
    {
        g_debug ("delete_items called successfully");
    }

    g_debug ("eas_sync_handler_delete_items--");
    return ret;
}

gboolean
eas_sync_handler_update_items (EasSyncHandler* self,
                               const gchar *sync_key_in,
                               gchar **sync_key_out,
                               EasItemType type,
                               const gchar* folder_id,
                               GSList *items_updated,
                               GError **error)
{
    gboolean ret = TRUE;
    DBusGProxy *proxy = self->priv->remoteEas;
    gchar *folder = NULL;
    gchar **updated_item_array = NULL;

    g_debug ("eas_sync_handler_update_items++");

	if(sync_key_in ==NULL || !g_strcmp0 (sync_key_in, "0"))
	{
		g_set_error (error, EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_BADARG,
                     ("delete_items requires a valid sync key"));
		return FALSE;
	}
	if(folder_id ==NULL)
	{
		//If folder_id is not set, then we set it to the default for the type
		switch(type)
		{
			case EAS_ITEM_CALENDAR:
			{
				folder = g_strdup("1");
			}
			break;
			default:
			{
				//unknown type -no default folder, set GError & return_type
			}
		}
	}
	else //use passed in folder_id
	{
		folder = g_strdup(folder_id);
	}


    g_debug ("eas_sync_handler_update_items - dbus proxy ok");

    g_debug ("server_id = %s", ( (EasCalInfo*) (items_updated->data))->server_id);

    build_serialised_calendar_info_array (&updated_item_array, items_updated, error);

    // call DBus API
    ret = dbus_g_proxy_call (proxy, "update_items", error,
                             G_TYPE_STRING, self->priv->account_uid,
                             G_TYPE_UINT64, (guint64) type,
                             G_TYPE_STRING, folder_id,
                             G_TYPE_STRING, sync_key_in,
                             G_TYPE_STRV, updated_item_array,
                             G_TYPE_INVALID,
                             G_TYPE_STRING, sync_key_out,
                             G_TYPE_INVALID);

    g_debug ("eas_sync_handler_update_items - dbus proxy called");
	g_free(folder);
    if (*error)
    {
        g_error (" Error: %s", (*error)->message);
    }

    if (ret)
    {
        g_debug ("update_calendar_items called successfully");
    }

    g_debug ("eas_sync_handler_update_items--");
    return ret;
}

// allocates an array of ptrs to strings and the strings it points to and populates each string with serialised folder details
// null terminates the array
static gboolean
build_serialised_calendar_info_array (gchar ***serialised_cal_info_array, const GSList *cal_list, GError **error)
{
    gboolean ret = TRUE;
    guint i = 0;
    GSList *l = (GSList*) cal_list;
    guint array_len = g_slist_length ( (GSList*) cal_list) + 1; //cast away const to avoid warning. +1 to allow terminating null

    g_debug ("build cal arrays++");
    g_assert (serialised_cal_info_array);
    g_assert (*serialised_cal_info_array == NULL);

    *serialised_cal_info_array = g_malloc0 (array_len * sizeof (gchar*));

    for (i = 0; i < array_len - 1; i++)
    {
        EasCalInfo *calInfo = l->data;
        gchar *tstring = NULL;
        g_assert (l != NULL);
        eas_cal_info_serialise (calInfo, &tstring);
        (*serialised_cal_info_array) [i] = tstring;
        l = g_slist_next (l);
    }

    return ret;
}

gboolean
eas_sync_handler_add_items (EasSyncHandler* self,
                            const gchar *sync_key_in,
                            gchar **sync_key_out,
                            EasItemType type,
                            const gchar* folder_id,
                            GSList *items_added,
                            GError **error)
{
    gboolean ret = TRUE;
    DBusGProxy *proxy = self->priv->remoteEas;
    gchar *folder = NULL;
    gchar **added_item_array = NULL;
    gchar **created_item_array = NULL;

    g_debug ("eas_sync_handler_add_items++");

	if(sync_key_in == NULL || !g_strcmp0 (sync_key_in, "0"))
	{
		g_set_error (error, EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_BADARG,
                     ("delete_items requires a valid sync key"));
		return FALSE;
	}
	if(folder_id ==NULL)
	{
		//If folder_id is not set, then we set it to the default for the type
		switch(type)
		{
			case EAS_ITEM_CALENDAR:
			{
				folder = g_strdup("1");
			}
			break;
			default:
			{
				//unknown type -no default folder, set GError & return_type
			}
		}
	}
	else //use passed in folder_id
	{
		folder = g_strdup(folder_id);
	}

    build_serialised_calendar_info_array (&added_item_array, items_added, error);

    // call DBus API
    ret = dbus_g_proxy_call (proxy, "add_items", error,
                             G_TYPE_STRING, self->priv->account_uid,
                             G_TYPE_UINT64, (guint64) type,
                             G_TYPE_STRING, folder_id,
                             G_TYPE_STRING, sync_key_in,
                             G_TYPE_STRV, added_item_array,
                             G_TYPE_INVALID,
                             G_TYPE_STRING, sync_key_out,
                             G_TYPE_STRV, &created_item_array,
                             G_TYPE_INVALID);

    g_debug ("eas_sync_handler_add_items - dbus proxy called");
	g_free(folder);
    if (*error)
    {
        g_error (" Error: %s", (*error)->message);
    }

    if (ret)
    {
        guint i = 0;
        guint length = g_slist_length (items_added);
        g_debug ("add_calendar_items called successfully");
        while (created_item_array[i] && i < length)
        {
            EasCalInfo *cal = eas_cal_info_new ();
            EasCalInfo *updated = g_slist_nth (items_added, i)->data;
            g_debug ("created item = %s", created_item_array[i]);
            eas_cal_info_deserialise (cal, created_item_array[i]);
            g_debug ("created item server id = %s", cal->server_id);
            updated->server_id = g_strdup (cal->server_id);
            g_debug ("created updated server id in list = %s", cal->server_id);
            i++;
        }
        if (i == length && created_item_array[i])
        {
            g_debug ("added list is not the same length as input list - problem?");
        }
    }

    g_debug ("eas_sync_handler_add_items--");
    return ret;
}







