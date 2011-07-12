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

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlsave.h>
#include <libedataserver/e-flag.h>

#include "../../eas-daemon/src/activesyncd-common-defs.h"
#include "libeasmail.h"
#include "eas-mail-client-stub.h"
#include "eas-folder.h"
#include "eas-mail-errors.h"
#include "../../logger/eas-logger.h"
#include "eas-marshal.h"

G_DEFINE_TYPE (EasEmailHandler, eas_mail_handler, G_TYPE_OBJECT);

#define EAS_EMAIL_HANDLER_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EAS_TYPE_EMAIL_HANDLER, EasEmailHandlerPrivate))

const gchar *updated_id_separator = ",";

static GStaticMutex progress_table = G_STATIC_MUTEX_INIT;

struct _EasEmailHandlerPrivate {
	DBusGConnection *bus;
	DBusGProxy *remoteEas;
	gchar* account_uid;     // TODO - is it appropriate to have a dbus proxy per account if we have multiple accounts making requests at same time?
	GMainLoop* main_loop;
	GHashTable *email_progress_fns_table;	// hashtable of request progress functions
	guint next_request_id;			// request id to be used for next dbus call

};

struct _EasProgressCallbackInfo {
	EasProgressFn progress_fn;
	gpointer progress_data;
	guint percent_last_sent;
};

typedef struct _EasProgressCallbackInfo EasProgressCallbackInfo;

struct _EasCommonParams {
	guint request_id;
	EasEmailHandler* self;
	GError **error;
};

typedef struct _EasCommonParams EasCommonParams;

struct _EasFetchEmailBodyParams {
	EasCommonParams base;
	const gchar *folder_id;
	const gchar *server_id;
	const gchar *mime_directory;
};

typedef struct _EasFetchEmailBodyParams EasFetchEmailBodyParams;


struct _EasFetchEmailAttachmentParams {
	EasCommonParams base;
	const gchar *file_reference;
	const gchar *mime_directory;
};

typedef struct _EasFetchEmailAttachmentParams EasFetchEmailAttachmentParams;

struct _EasSendEmailParams {
	EasCommonParams base;
	const gchar *client_email_id;   // unique message identifier supplied by client
	const gchar *mime_file; 		// the full path to the email (mime) to be sent
};

typedef struct _EasSendEmailParams EasSendEmailParams;

struct _EasDBusCompleteParams {
	GMainLoop* main_loop;
	GError **error;
};

typedef struct _EasDBusCompleteParams EasDBusCompleteParams;

// fwd declaration
static void progress_signal_handler (DBusGProxy * proxy,
				     guint request_id,
				     guint percent,
				     gpointer user_data);

// TODO - how much verification of args should happen??

static void
eas_mail_handler_init (EasEmailHandler *cnc)
{
	EasEmailHandlerPrivate *priv;
	g_debug ("eas_mail_handler_init++");

	/* allocate internal structure */
	cnc->priv = priv = EAS_EMAIL_HANDLER_PRIVATE (cnc);

	priv->remoteEas = NULL;
	priv->bus = NULL;
	priv->account_uid = NULL;
	priv->main_loop = NULL;
	priv->next_request_id = 1;
	priv->email_progress_fns_table = NULL;
	g_debug ("eas_mail_handler_init--");
}

static void
eas_mail_handler_finalize (GObject *object)
{
	EasEmailHandler *cnc = (EasEmailHandler *) object;
	EasEmailHandlerPrivate *priv;
	g_debug ("eas_mail_handler_finalize++");

	priv = cnc->priv;

	// register for progress signals
	dbus_g_proxy_disconnect_signal (cnc->priv->remoteEas,
					EAS_MAIL_SIGNAL_PROGRESS,
					G_CALLBACK (progress_signal_handler),
					cnc);

	g_free (priv->account_uid);

	g_main_loop_quit (priv->main_loop);
	dbus_g_connection_unref (priv->bus);
	// free the hashtable
	if (priv->email_progress_fns_table) {
		g_hash_table_remove_all (priv->email_progress_fns_table);
	}
	// nothing to do to 'free' proxy

	G_OBJECT_CLASS (eas_mail_handler_parent_class)->finalize (object);
	g_debug ("eas_mail_handler_finalize--");
}

static void
eas_mail_handler_class_init (EasEmailHandlerClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_debug ("eas_mail_handler_class_init++");

	g_type_class_add_private (klass, sizeof (EasEmailHandlerPrivate));

	object_class->finalize = eas_mail_handler_finalize;
	g_debug ("eas_mail_handler_class_init--");
}


static gboolean
eas_mail_add_progress_info_to_table (EasEmailHandler* self, guint request_id, EasProgressFn progress_fn, gpointer progress_data, GError **error)
{
	gboolean ret = TRUE;
	EasEmailHandlerPrivate *priv = self->priv;
	EasProgressCallbackInfo *progress_info = g_malloc0 (sizeof (EasProgressCallbackInfo));

	if (!progress_info) {
		g_set_error (error, EAS_MAIL_ERROR,
			     EAS_MAIL_ERROR_NOTENOUGHMEMORY,
			     ("out of memory"));
		ret = FALSE;
		goto finish;
	}

	// add progress fn/data structure to hash table
	progress_info->progress_fn = progress_fn;
	progress_info->progress_data = progress_data;
	progress_info->percent_last_sent = 0;

	g_static_mutex_lock (&progress_table);	
	if (priv->email_progress_fns_table == NULL) {
		priv->email_progress_fns_table = g_hash_table_new_full (NULL, NULL, NULL, g_free);
	}
	g_debug ("insert progress function into table");
	g_hash_table_insert (priv->email_progress_fns_table, (gpointer) request_id, progress_info);
	g_static_mutex_unlock (&progress_table);
finish:
	return ret;
}

EasEmailHandler *
eas_mail_handler_new (const char* account_uid, GError **error)
{
	EasEmailHandler *object = NULL;
	EasEmailHandlerPrivate *priv = NULL;

	g_type_init();

	dbus_g_thread_init();

	g_log_set_handler (G_LOG_DOMAIN,
			   G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL,
			   eas_logger,
			   NULL);

	g_debug ("eas_mail_handler_new++ : account_uid[%s]", (account_uid ? account_uid : "NULL"));

	if (!account_uid) {
		g_set_error (error, EAS_MAIL_ERROR, EAS_MAIL_ERROR_UNKNOWN,
			     "No account UID specified");
		return NULL;
	}

	object = g_object_new (EAS_TYPE_EMAIL_HANDLER, NULL);

	if (object == NULL) {
		g_set_error (error, EAS_MAIL_ERROR, EAS_MAIL_ERROR_UNKNOWN,
			     "Could not create email handler object");
		g_warning ("Error: Couldn't create mail");
		g_debug ("eas_mail_handler_new--");
		return NULL;
	}
	priv = object->priv;
	priv->main_loop = g_main_loop_new (NULL, FALSE);

	if (priv->main_loop == NULL) {
		g_set_error (error, EAS_MAIL_ERROR, EAS_MAIL_ERROR_UNKNOWN,
			     "Failed to create mainloop");
		return NULL;
	}

	g_debug ("Connecting to Session D-Bus.");
	priv->bus = dbus_g_bus_get (DBUS_BUS_SESSION, error);
	if (priv->bus == NULL) {
		g_warning ("Error: Couldn't connect to the Session bus (%s) ", error ? (*error)->message : "<discarded error>");
		return NULL;
	}

	g_debug ("Creating a GLib proxy object for Eas.");
	priv->remoteEas =  dbus_g_proxy_new_for_name (priv->bus,
							      EAS_SERVICE_NAME,
							      EAS_SERVICE_MAIL_OBJECT_PATH,
							      EAS_SERVICE_MAIL_INTERFACE);
	if (priv->remoteEas == NULL) {
		g_set_error (error, EAS_MAIL_ERROR, EAS_MAIL_ERROR_UNKNOWN,
			     "Failed to create mainloop");
		g_warning ("Error: Couldn't create the proxy object");
		return NULL;
	}

	dbus_g_proxy_set_default_timeout (priv->remoteEas, 1000000);
	priv->account_uid = g_strdup (account_uid);


	/* Register dbus signal marshaller */
	dbus_g_object_register_marshaller (eas_marshal_VOID__UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_UINT,
					   G_TYPE_INVALID);

	g_debug ("register as observer of %s signal", EAS_MAIL_SIGNAL_PROGRESS);
	// progress signal setup:
	dbus_g_proxy_add_signal (priv->remoteEas,
				 EAS_MAIL_SIGNAL_PROGRESS,
				 G_TYPE_UINT,	// request id
				 G_TYPE_UINT,	// percent
				 G_TYPE_INVALID);

	// register for progress signals
	dbus_g_proxy_connect_signal (priv->remoteEas,
				     EAS_MAIL_SIGNAL_PROGRESS,
				     G_CALLBACK (progress_signal_handler),		// callback when signal emitted
				     object,													// userdata passed to above cb
				     NULL);

	g_debug ("eas_mail_handler_new--");
	return object;
}

// takes an NULL terminated array of serialised folders and creates a list of EasFolder objects
static gboolean
build_folder_list (const gchar **serialised_folder_array, GSList **folder_list, GError **error)
{
	gboolean ret = TRUE;
	guint i = 0;

	g_debug ("build_folder_list++");
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_assert (folder_list);
	g_assert (g_slist_length (*folder_list) == 0);

	while (serialised_folder_array[i]) {
		EasFolder *folder = eas_folder_new();
		if (folder) {
			*folder_list = g_slist_append (*folder_list, folder);   // add it to the list first to aid cleanup
			if (!folder_list) {
				g_free (folder);
				ret = FALSE;
				goto cleanup;
			}
			if (!eas_folder_deserialise (folder, serialised_folder_array[i])) {
				ret = FALSE;
				goto cleanup;
			}
		} else {
			ret = FALSE;
			goto cleanup;
		}
		i++;
	}

cleanup:
	if (!ret) {
		// set the error
		g_set_error (error, EAS_MAIL_ERROR,
			     EAS_MAIL_ERROR_NOTENOUGHMEMORY,
			     ("out of memory"));
		// clean up on error
		g_slist_foreach (*folder_list, (GFunc) g_free, NULL);
		g_slist_free (*folder_list);
	}

	g_debug ("list has %d items", g_slist_length (*folder_list));
	g_debug ("build_folder_list++");
	return ret;
}


// takes an NULL terminated array of serialised emailinfos and creates a list of EasEmailInfo objects
static gboolean
build_emailinfo_list (const gchar **serialised_emailinfo_array, GSList **emailinfo_list, GError **error)
{
	gboolean ret = TRUE;
	guint i = 0;
	g_debug ("build_emailinfo_list++");

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_assert (g_slist_length (*emailinfo_list) == 0);

	while (serialised_emailinfo_array[i]) {
		EasEmailInfo *emailinfo = eas_email_info_new ();
		if (emailinfo) {
			*emailinfo_list = g_slist_append (*emailinfo_list, emailinfo);
			if (!*emailinfo_list) {
				g_free (emailinfo);
				ret = FALSE;
				goto cleanup;
			}
			if (!eas_email_info_deserialise (emailinfo, serialised_emailinfo_array[i])) {
				ret = FALSE;
				goto cleanup;
			}
		} else {
			ret = FALSE;
			goto cleanup;
		}
		i++;
	}

cleanup:
	if (!ret) {
		// set the error
		g_set_error (error, EAS_MAIL_ERROR,
			     EAS_MAIL_ERROR_NOTENOUGHMEMORY,
			     ("out of memory"));
		// clean up on error
		g_slist_foreach (*emailinfo_list, (GFunc) g_object_unref, NULL);
		g_slist_free (*emailinfo_list);
		*emailinfo_list = NULL;
	}

	g_debug ("build_emailinfo_list--");
	return ret;
}

/*
take the contents of the structure and turn it into a null terminated string
*/
gboolean
eas_updatedid_serialise (const EasIdUpdate* updated_id, gchar **result)
{
	gboolean ret = TRUE;

	gchar *strings[3];

	g_debug ("eas_updatedid_serialise++");

	strings[0] = updated_id->src_id;
	strings[1] = updated_id->dest_id;
	strings[2] = NULL;

	*result = g_strjoinv (updated_id_separator, strings);

	if (*result == NULL) {
		ret = FALSE;
	}
	g_debug ("eas_updatedid_serialise--");
	return ret;
}

/*
populate the object from a string
*/
static gboolean
eas_updatedid_deserialise (EasIdUpdate *updated_id, const gchar* data)
{
	gboolean ret = FALSE;
	gchar **strv;
	gchar *from = (gchar*) data;

	g_debug ("eas_updatedid_deserialise++");
	g_assert (updated_id);
	g_assert (data);
	g_assert (updated_id->dest_id == NULL);
	g_assert (updated_id->src_id == NULL);

	strv = g_strsplit (data, updated_id_separator, 0);
	if (!strv || g_strv_length (strv) > 2) {
		g_warning ("Received invalid updateid: '%s'", data);
		g_strfreev (strv);
		goto out;
	}

	ret = TRUE;
	updated_id->src_id = strv[0];
	/* This one might be NULL; that's OK */
	updated_id->dest_id = strv[1];

	g_free (strv);

 out:
	g_debug ("eas_updatedid_deserialise++");
	return ret;
}

// converts an NULL terminated array of serialised EasIdUpdates to a list
static gboolean
build_easidupdates_list (const gchar **updated_ids_array, GSList **updated_ids_list, GError **error)
{
	gboolean ret = TRUE;
	guint i = 0;
	g_debug ("build_easidupdates_list++");

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_assert (g_slist_length (*updated_ids_list) == 0);

	while (updated_ids_array[i]) {
		EasIdUpdate *updated_id = g_malloc0 (sizeof (EasIdUpdate));
		if (updated_id) {
			*updated_ids_list = g_slist_append (*updated_ids_list, updated_id);
			if (!*updated_ids_list) {
				g_free (updated_id);
				ret = FALSE;
				goto cleanup;
			}
			if (!eas_updatedid_deserialise (updated_id, updated_ids_array[i])) {
				ret = FALSE;
				goto cleanup;
			}
		} else {
			ret = FALSE;
			goto cleanup;
		}
		i++;
	}

cleanup:
	if (!ret) {
		// set the error
		g_set_error (error, EAS_MAIL_ERROR,
			     EAS_MAIL_ERROR_NOTENOUGHMEMORY,
			     ("out of memory"));
		// clean up on error
		g_slist_foreach (*updated_ids_list, (GFunc) g_free, NULL);
		g_slist_free (*updated_ids_list);
		*updated_ids_list = NULL;
	}

	g_debug ("build_easidupdates_list++");
	return ret;
}

// TODO remove and use..._strfreev?
static void
free_string_array (gchar **array)
{
	guint i = 0;

	if (array == NULL)
		return;

	while (array[i]) {
		g_free (array[i]);
		i++;
	}
	g_free (array);

}

// pulls down changes in folder structure (folders added/deleted/updated). Supplies lists of EasFolders
gboolean
eas_mail_handler_sync_folder_hierarchy (EasEmailHandler* self,
					gchar *sync_key,
					GSList **folders_created,
					GSList **folders_updated,
					GSList **folders_deleted,
					GError **error)
{
	gboolean ret = TRUE;
	DBusGProxy *proxy = self->priv->remoteEas;
	gchar **created_folder_array = NULL;
	gchar **deleted_folder_array = NULL;
	gchar **updated_folder_array = NULL;
	gchar *updatedSyncKey = NULL;

	g_debug ("eas_mail_handler_sync_folder_hierarchy++ : account_uid[%s]",
		 (self->priv->account_uid ? self->priv->account_uid : "NULL"));

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_assert (self);
	g_assert (sync_key);
	g_assert (g_slist_length (*folders_created) == 0);
	g_assert (g_slist_length (*folders_updated) == 0);
	g_assert (g_slist_length (*folders_deleted) == 0);

	// call DBus API
	ret = dbus_g_proxy_call (proxy, "sync_email_folder_hierarchy",
				 error,
				 G_TYPE_STRING, self->priv->account_uid,
				 G_TYPE_STRING, sync_key,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &updatedSyncKey,
				 G_TYPE_STRV, &created_folder_array,
				 G_TYPE_STRV, &deleted_folder_array,
				 G_TYPE_STRV, &updated_folder_array,
				 G_TYPE_INVALID);

	g_debug ("eas_mail_handler_sync_folder_hierarchy - dbus proxy called");

	if (!ret) {
		if (error && *error) {
			g_warning ("[%s][%d][%s]",
				   g_quark_to_string ( (*error)->domain),
				   (*error)->code,
				   (*error)->message);
		}
		g_warning ("DBus dbus_g_proxy_call failed");
		goto cleanup;
	}

	g_debug ("sync_email_folder_hierarchy called successfully");

	// put the updated sync key back into the original string for tracking this
	strcpy (sync_key, updatedSyncKey);

	// get 3 arrays of strings of 'serialised' EasFolders, convert to EasFolder lists:
	ret = build_folder_list ( (const gchar **) created_folder_array, folders_created, error);
	if (!ret) {
		goto cleanup;
	}
	ret = build_folder_list ( (const gchar **) deleted_folder_array, folders_deleted, error);
	if (!ret) {
		goto cleanup;
	}
	ret = build_folder_list ( (const gchar **) updated_folder_array, folders_updated, error);
	if (!ret) {
		goto cleanup;
	}


cleanup:

	g_free (updatedSyncKey);
	free_string_array (created_folder_array);
	free_string_array (updated_folder_array);
	free_string_array (deleted_folder_array);

	if (!ret) { // failed - cleanup lists
		g_assert (error == NULL || *error != NULL);
		if (error) {
			g_warning (" Error: %s", (*error)->message);
		}
		g_debug ("eas_mail_handler_sync_folder_hierarchy failure - cleanup lists");
		g_slist_foreach (*folders_created, (GFunc) g_free, NULL);
		g_free (*folders_created);
		*folders_created = NULL;
		g_slist_foreach (*folders_updated, (GFunc) g_free, NULL);
		g_free (*folders_updated);
		*folders_updated = NULL;
		g_slist_foreach (*folders_deleted, (GFunc) g_free, NULL);
		g_free (*folders_deleted);
		*folders_deleted = NULL;
	}

	g_debug ("eas_mail_handler_sync_folder_hierarchy--");
	return ret;
}


/* sync emails in a specified folder (no bodies retrieved).
Returns lists of EasEmailInfos.
Max changes in one sync = 100 (configurable via a config api)
In the case of created emails all fields are filled in.
In the case of deleted emails only the serverids are valid.
In the case of updated emails only the serverids, flags and categories are valid.
*/
gboolean
eas_mail_handler_sync_folder_email_info (EasEmailHandler* self,
					 gchar *sync_key,
					 const gchar *collection_id, // folder to sync
					 GSList **emailinfos_created,
					 GSList **emailinfos_updated,
					 GSList **emailinfos_deleted,
					 gboolean *more_available,   // if there are more changes to sync (window_size exceeded)
					 GError **error)
{
	gboolean ret = TRUE;
	DBusGProxy *proxy = self->priv->remoteEas;
	gchar **created_emailinfo_array = NULL;
	gchar **deleted_emailinfo_array = NULL;
	gchar **updated_emailinfo_array = NULL;
	gchar *updatedSyncKey = NULL;

	g_debug ("eas_mail_handler_sync_folder_email_info++");
	g_debug ("sync_key = %s", sync_key);
	g_assert (self);
	g_assert (sync_key);
	g_assert (collection_id);
	g_assert (more_available);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_debug ("eas_mail_handler_sync_folder_email_info about to call dbus proxy");
	// call dbus api with appropriate params
	ret = dbus_g_proxy_call (proxy, "sync_folder_email", error,
				 G_TYPE_STRING, self->priv->account_uid,
				 G_TYPE_STRING, sync_key,
				 G_TYPE_STRING, collection_id,           // folder
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &updatedSyncKey,
				 G_TYPE_BOOLEAN, more_available,
				 G_TYPE_STRV, &created_emailinfo_array,
				 G_TYPE_STRV, &deleted_emailinfo_array,
				 G_TYPE_STRV, &updated_emailinfo_array,
				 G_TYPE_INVALID);

	g_debug ("eas_mail_handler_sync_folder_email_info called proxy");
	// convert created/deleted/updated emailinfo arrays into lists of emailinfo objects (deserialise results)
	if (ret) {
		g_debug ("sync_folder_email called successfully");

		// put the updated sync key back into the original string for tracking this
		/* When we get a null response (only headers because there were no changes),
		   do *NOT* overwrite the existing sync key! */
		if (updatedSyncKey && updatedSyncKey[0])
			strcpy (sync_key, updatedSyncKey);

		// get 3 arrays of strings of 'serialised' EasEmailInfos, convert to EasEmailInfo lists:
		ret = build_emailinfo_list ( (const gchar **) created_emailinfo_array, emailinfos_created, error);
		if (ret) {
			ret = build_emailinfo_list ( (const gchar **) deleted_emailinfo_array, emailinfos_deleted, error);
		}
		if (ret) {
			ret = build_emailinfo_list ( (const gchar **) updated_emailinfo_array, emailinfos_updated, error);
		}
	}

	if (updatedSyncKey) {
		g_free (updatedSyncKey);
	}
	free_string_array (created_emailinfo_array);
	free_string_array (updated_emailinfo_array);
	free_string_array (deleted_emailinfo_array);

	if (!ret) { // failed - cleanup lists
		g_assert (error == NULL || *error != NULL);
		g_slist_foreach (*emailinfos_created, (GFunc) g_object_unref, NULL);
		g_slist_free (*emailinfos_created);
		*emailinfos_created = NULL;
		g_slist_foreach (*emailinfos_updated, (GFunc) g_object_unref, NULL);
		g_slist_free (*emailinfos_updated);
		*emailinfos_updated = NULL;
		g_slist_foreach (*emailinfos_deleted, (GFunc) g_object_unref, NULL);
		g_slist_free (*emailinfos_deleted);
		*emailinfos_deleted = NULL;
	}
	g_debug ("eas_mail_handler_sync_folder_email_info--");
	g_debug ("sync_key = %s", sync_key);

	return ret;
}


static void
progress_signal_handler (DBusGProxy* proxy,
			 guint request_id,
			 guint percent,
			 gpointer user_data)
{
	EasProgressCallbackInfo *progress_callback_info;
	EasEmailHandler* self = (EasEmailHandler*) user_data;

	g_debug ("progress_signal_handler++");

	if ( (self->priv->email_progress_fns_table) && (percent > 0)) {
		// if there's a progress function for this request in our hashtable, call it:
		g_static_mutex_lock (&progress_table);
		progress_callback_info = g_hash_table_lookup (self->priv->email_progress_fns_table, (gpointer) request_id);
		g_static_mutex_unlock (&progress_table);
		if (progress_callback_info) {
			if (percent > progress_callback_info->percent_last_sent) {
				EasProgressFn progress_fn = (EasProgressFn) (progress_callback_info->progress_fn);

				g_debug ("call progress function with %d%c", percent, '%');
				progress_callback_info->percent_last_sent = percent;

				progress_fn (progress_callback_info->progress_data, percent);
			}
		}
	}

	g_debug ("progress_signal_handler--");
	return;
}


static void
dbus_call_completed (DBusGProxy* proxy, DBusGProxyCall* call, gpointer user_data)
{
	EasDBusCompleteParams *params = (EasDBusCompleteParams *) user_data;
	gboolean ret;
	GMainLoop *loop = params->main_loop;
	GError **error = params->error;

	g_debug ("dbus call completed");

	g_free (user_data);

	// blocks until results are available:
	ret = dbus_g_proxy_end_call (proxy,
				     call,
				     error,
				     G_TYPE_INVALID);

	if (loop) {
		g_debug ("quit main loop");
		g_main_loop_quit (loop);
	}

	return;
}


static gboolean
begin_call_fetch_email_body (gpointer data)
{
	DBusGProxyCall *call;
	EasFetchEmailBodyParams *params = (EasFetchEmailBodyParams *) data;
	EasEmailHandler* self = params->base.self;
	EasEmailHandlerPrivate* priv = self->priv;

	EasDBusCompleteParams *cb_data = g_new0 (EasDBusCompleteParams, 1);
	cb_data->error = params->base.error;
	cb_data->main_loop = priv->main_loop;

	call = dbus_g_proxy_begin_call (priv->remoteEas, "fetch_email_body",
					dbus_call_completed,
					cb_data, 					// userdata passed to callback
					NULL, 							// destroy notification
					G_TYPE_STRING, priv->account_uid,
					G_TYPE_STRING, params->folder_id,
					G_TYPE_STRING, params->server_id,
					G_TYPE_STRING, params->mime_directory,
					G_TYPE_UINT, params->base.request_id,
					G_TYPE_INVALID);

	g_free (params);

	return FALSE;	// source should be removed from the loop
}


static gboolean
begin_call_fetch_email_attachment (gpointer data)
{
	DBusGProxyCall *call;
	EasFetchEmailAttachmentParams *params = (EasFetchEmailAttachmentParams *) data;
	EasEmailHandler* self = params->base.self;
	EasEmailHandlerPrivate* priv = self->priv;

	EasDBusCompleteParams *cb_data = g_new0 (EasDBusCompleteParams, 1);
	cb_data->error = params->base.error;
	cb_data->main_loop = priv->main_loop;

	call = dbus_g_proxy_begin_call (priv->remoteEas, "fetch_attachment",
					dbus_call_completed,
					cb_data, 							// userdata
					NULL, 								// destroy notification
					G_TYPE_STRING, priv->account_uid,
					G_TYPE_STRING, params->file_reference,
					G_TYPE_STRING, params->mime_directory,
					G_TYPE_UINT, params->base.request_id,
					G_TYPE_INVALID);

	g_free (params);

	return FALSE;	// source should be removed from the loop
}


static gboolean
begin_call_send_email (gpointer data)
{
	DBusGProxyCall *call;
	EasSendEmailParams *params = (EasSendEmailParams *) data;
	EasEmailHandler* self = params->base.self;
	EasEmailHandlerPrivate* priv = self->priv;

	EasDBusCompleteParams *cb_data = g_new0 (EasDBusCompleteParams, 1);
	cb_data->error = params->base.error;
	cb_data->main_loop = priv->main_loop;

	call = dbus_g_proxy_begin_call (priv->remoteEas, "send_email",
					dbus_call_completed,
					cb_data, 							// userdata
					NULL, 								// destroy notification
					G_TYPE_STRING, priv->account_uid,
					G_TYPE_STRING, params->client_email_id,
					G_TYPE_STRING, params->mime_file,
					G_TYPE_UINT, params->base.request_id,
					G_TYPE_INVALID);

	g_free (params);

	return FALSE;	// source should be removed from the loop
}


// get the entire email body for listed email
// email body will be written to a file with the emailid as its name
gboolean
eas_mail_handler_fetch_email_body (EasEmailHandler* self,
				   const gchar *folder_id,
				   const gchar *server_id,
				   const gchar *mime_directory,
				   EasProgressFn progress_fn,
				   gpointer progress_data,
				   GError **error)
{
	gboolean ret = TRUE;
	EasFetchEmailBodyParams *data;
	EasEmailHandlerPrivate *priv = self->priv;
	guint request_id;

	g_debug ("eas_mail_handler_fetch_email_body++");
	g_assert (self);
	g_assert (folder_id);
	g_assert (server_id);
	g_assert (mime_directory);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	// if there's a progress function supplied, add it (and the progress_data) to the hashtable, indexed by id
	request_id = priv->next_request_id++;

	if (progress_fn) {
		ret = eas_mail_add_progress_info_to_table (self, request_id, progress_fn, progress_data, error);
		if (!ret)
			goto finish;
	}

	data = g_new0 (EasFetchEmailBodyParams, 1);

	data->base.error = error;
	data->base.request_id = request_id;
	data->mime_directory = mime_directory;
	data->server_id = server_id;
	data->folder_id = folder_id;
	data->base.self = self;

	g_idle_add (begin_call_fetch_email_body, data);

	g_main_loop_run (priv->main_loop);

	g_debug ("main loop has quit");
	if (error != NULL && *error != NULL)
		ret = FALSE;

	g_debug ("eas_mail_handler_fetch_email_body--");

finish:
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
	}
	return ret;
}


gboolean
eas_mail_handler_fetch_email_attachment (EasEmailHandler* self,
					 const gchar *file_reference,
					 const gchar *mime_directory,
					 EasProgressFn progress_fn,
					 gpointer progress_data,
					 GError **error)
{
	gboolean ret = TRUE;
	EasEmailHandlerPrivate *priv = self->priv;
	guint request_id;
	EasFetchEmailAttachmentParams *data;

	g_debug ("eas_mail_handler_fetch_email_attachment++");
	g_assert (self);
	g_assert (file_reference);
	g_assert (mime_directory);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	// if there's a progress function supplied, add it (and the progress_data) to the hashtable, indexed by id
	request_id = priv->next_request_id++;

	if (progress_fn) {
		ret = eas_mail_add_progress_info_to_table (self, request_id, progress_fn, progress_data, error);
		if (!ret)
			goto finish;
	}

	data = g_new0 (EasFetchEmailAttachmentParams, 1);
	data->mime_directory = mime_directory;
	data->file_reference = file_reference;
	data->base.request_id = request_id;
	data->base.error = error;
	data->base.self = self;

	g_idle_add (begin_call_fetch_email_attachment, data);

	g_main_loop_run (priv->main_loop);

	g_debug ("main loop has quit");
	if (error != NULL && *error != NULL)
		ret = FALSE;

finish:
	g_debug ("eas_mail_handler_fetch_email_attachments--");

	if (!ret) {
		g_assert (error == NULL || *error != NULL);
	}
	return ret;
}


// Delete specified emails from a single folder
gboolean
eas_mail_handler_delete_email (EasEmailHandler* self,
			       const gchar *sync_key,            // sync_key for the folder containing these emails
			       const gchar *folder_id,     // folder that contains email to delete
			       const GSList *items_deleted,        // emails to delete
			       GError **error)
{
	gboolean ret = TRUE;
	DBusGProxy *proxy = self->priv->remoteEas;
	gchar *updatedSyncKey = NULL;
	gchar **deleted_items_array = NULL;
	// Build string array from items_deleted GSList
	guint list_length = g_slist_length ( (GSList*) items_deleted);
	int loop = 0;

	g_debug ("eas_mail_handler_delete_emails++");
	g_assert (self);
	g_assert (sync_key);
	g_assert (items_deleted);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	deleted_items_array = g_malloc0 ( (list_length + 1) * sizeof (gchar*));
	if (!deleted_items_array) {
		g_set_error (error, EAS_MAIL_ERROR,
			     EAS_MAIL_ERROR_NOTENOUGHMEMORY,
			     ("out of memory"));
		goto finish;
	}

	for (; loop < list_length; ++loop) {
		deleted_items_array[loop] = g_strdup (g_slist_nth_data ( (GSList*) items_deleted, loop));
		g_debug ("Deleted Id: [%s]", deleted_items_array[loop]);
	}

	ret = dbus_g_proxy_call (proxy, "delete_email", error,
				 G_TYPE_STRING, self->priv->account_uid,
				 G_TYPE_STRING, sync_key,
				 G_TYPE_STRING, folder_id,
				 G_TYPE_STRV, deleted_items_array,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &updatedSyncKey,
				 G_TYPE_INVALID);

	// Clean up string array
	for (loop = 0; loop < list_length; ++loop) {
		g_free (deleted_items_array[loop]);
		deleted_items_array[loop] = NULL;
	}
	g_free (deleted_items_array);
	deleted_items_array = NULL;

finish:
	if (ret) {
		// put the updated sync key back into the original string for tracking this
		// strcpy(sync_key,updatedSyncKey);
	} else {
		g_assert (error == NULL || *error != NULL);
	}

	if (updatedSyncKey) {
		g_free (updatedSyncKey);
	}

	g_debug ("eas_mail_handler_delete_emails--");
	return ret;
}


/*
'push' email updates to server
Note that the only valid changes are to the read flag and to categories (other changes ignored)
TODO - should this be changed to support updating multiple emails at once?
*/
gboolean
eas_mail_handler_update_email (EasEmailHandler* self,
			       const gchar *sync_key,            // sync_key for the folder containing the emails
			       const gchar *folder_id,     // folder that contains email to delete
			       const GSList *update_emails,        // emails to update
			       GError **error)
{
	gboolean ret = TRUE;
	DBusGProxy *proxy = self->priv->remoteEas;
	// serialise the emails
	guint num_emails = g_slist_length ( (GSList *) update_emails);
	gchar **serialised_email_array = g_malloc0 ( (num_emails * sizeof (gchar*)) + 1);  // null terminated array of strings
	gchar *serialised_email = NULL;
	guint i;
	GSList *l = (GSList *) update_emails;

	g_debug ("eas_mail_handler_update_emails++");
	g_assert (self);
	g_assert (sync_key);
	g_assert (update_emails);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_debug ("sync_key = %s", sync_key);
	g_debug ("%d emails to update", num_emails);

	for (i = 0; i < num_emails; i++) {
		EasEmailInfo *email = l->data;

		g_debug ("serialising email %d", i);
		ret = eas_email_info_serialise (email, &serialised_email);
		if (!ret) {
			// set the error
			g_set_error (error, EAS_MAIL_ERROR,
				     EAS_MAIL_ERROR_NOTENOUGHMEMORY,
				     ("out of memory"));
			serialised_email_array[i] = NULL;
			goto cleanup;
		}
		serialised_email_array[i] = serialised_email;
		g_debug ("serialised_email_array[%d] = %s", i, serialised_email_array[i]);

		l = l->next;
	}
	serialised_email_array[i] = NULL;

	// call dbus api
	ret = dbus_g_proxy_call (proxy, "update_emails", error,
				 G_TYPE_STRING, self->priv->account_uid,
				 G_TYPE_STRING, sync_key,
				 G_TYPE_STRING, folder_id,
				 G_TYPE_STRV, serialised_email_array,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);

cleanup:
	// free all strings in the array
	for (i = 0; i < num_emails && serialised_email_array[i]; i++) {
		g_free (serialised_email_array[i]);
	}
	g_free (serialised_email_array);

	if (!ret) {
		g_assert (error == NULL || *error != NULL);
	}

	g_debug ("eas_mail_handler_update_emails--");

	return ret;
}


gboolean
eas_mail_handler_send_email (EasEmailHandler* self,
			     const gchar *client_email_id,   // unique message identifier supplied by client
			     const gchar *mime_file,         // the full path to the email (mime) to be sent
			     EasProgressFn progress_fn,
			     gpointer progress_data,
			     GError **error)
{
	gboolean ret = TRUE;
	EasEmailHandlerPrivate *priv = self->priv;
	guint request_id;
	EasSendEmailParams *data;

	g_debug ("eas_mail_handler_send_email++");
	g_assert (self);
	g_assert (client_email_id);
	g_assert (mime_file);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	// if there's a progress function supplied, add it (and the progress_data) to the hashtable, indexed by id
	request_id = priv->next_request_id++;

	if (progress_fn) {
		ret = eas_mail_add_progress_info_to_table (self, request_id, progress_fn, progress_data, error);
		if (!ret)
			goto finish;
	}

	data = g_new0 (EasSendEmailParams, 1);
	data->mime_file = mime_file;
	data->client_email_id = client_email_id;
	data->base.request_id = request_id;
	data->base.error = error;
	data->base.self = self;

	g_idle_add (begin_call_send_email, data);

	g_main_loop_run (priv->main_loop);

	g_debug ("main loop has quit");
	if (error != NULL && *error != NULL)
		ret = FALSE;

finish:
	g_debug ("eas_mail_handler_send_email--");

	if (!ret) {
		g_assert (error == NULL || *error != NULL);
	}
	return ret;
}

gboolean
eas_mail_handler_move_to_folder (EasEmailHandler* self,
				 const GSList *server_ids,
				 const gchar *src_folder_id,
				 const gchar *dest_folder_id,
				 GSList **updated_ids_list,
				 GError **error)
{
	gboolean ret = TRUE;
	DBusGProxy *proxy = self->priv->remoteEas;
	gchar **updated_ids_array = NULL;
	gchar **server_ids_array = NULL;
	guint i = 0;
	guint list_len = g_slist_length ( (GSList*) server_ids);

	g_debug ("eas_mail_handler_move_to_folder++");

	g_assert (self);
	g_assert (server_ids);
	g_assert (src_folder_id);
	g_assert (dest_folder_id);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	// convert lists to array for passing over dbus
	server_ids_array = g_malloc0 ( (list_len + 1) * sizeof (gchar*));
	if (!server_ids_array) {
		ret = FALSE;
		g_set_error (error, EAS_MAIL_ERROR,
			     EAS_MAIL_ERROR_NOTENOUGHMEMORY,
			     ("out of memory"));
		goto finish;
	}

	for (; i < list_len; ++i) {
		server_ids_array[i] = g_strdup (g_slist_nth_data ( (GSList*) server_ids, i));
		g_debug ("server Id: [%s]", server_ids_array[i]);
	}

// call dbus api
	ret = dbus_g_proxy_call (proxy, "move_emails_to_folder", error,
				 G_TYPE_STRING, self->priv->account_uid,
				 G_TYPE_STRV, server_ids_array,
				 G_TYPE_STRING, src_folder_id,
				 G_TYPE_STRING, dest_folder_id,
				 G_TYPE_INVALID,
				 G_TYPE_STRV, &updated_ids_array,
				 G_TYPE_INVALID);

	// Clean up string array
	for (i = 0; i < list_len; ++i) {
		g_free (server_ids_array[i]);
		server_ids_array[i] = NULL;
	}
	g_free (server_ids_array);
	server_ids_array = NULL;

	if (ret) {
		ret = build_easidupdates_list ( (const gchar**) updated_ids_array, updated_ids_list, error);	//why does this require a cast?
	}
	g_strfreev (updated_ids_array);

finish:
	if (!ret) {
		g_assert (error == NULL || *error != NULL);
	}
	g_debug ("eas_mail_handler_move_to_folder--");
	return ret;
}

// TODO How supported in AS?
gboolean
eas_mail_handler_copy_to_folder (EasEmailHandler* self,
				 const GSList *email_ids,
				 const gchar *src_folder_id,
				 const gchar *dest_folder_id,
				 GError **error)
{
	gboolean ret = TRUE;
	g_debug ("eas_mail_handler_copy_to_folder++");

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_debug ("eas_mail_handler_copy_to_folder--");

	if (!ret) {
		g_assert (error == NULL || *error != NULL);
	}
	return ret;
	/* TODO */
}

static void watch_email_folder_completed (DBusGProxy* proxy, DBusGProxyCall* call, gpointer user_data)
{
	GError *error = NULL;
	gchar* id;
	gchar **changed_folder_array = NULL;
	gint index = 0;
	GSList *folder_list = NULL;
	EasPushEmailCallback callback = NULL;

	g_debug ("watch_email_folder_completed");

	dbus_g_proxy_end_call (proxy, call, &error,
			       G_TYPE_STRV, &changed_folder_array,
			       G_TYPE_INVALID);

	while ( (id = changed_folder_array[index++])) {
		g_debug ("Folder id = [%s]", id);
		folder_list = g_slist_prepend (folder_list, g_strdup (id));
	}

	callback = (EasPushEmailCallback) (user_data);
	callback (folder_list, error);

	return;
}

gboolean eas_mail_handler_watch_email_folders (EasEmailHandler* self,
					       const GSList *folder_ids,
					       const gchar *heartbeat,
					       EasPushEmailCallback cb,
					       GError **error)
{
	gboolean ret = TRUE;
	DBusGProxy *proxy = self->priv->remoteEas;
	gchar **folder_array = NULL;
	// Build string array from items_deleted GSList
	guint list_length = g_slist_length ( (GSList*) folder_ids);
	int loop = 0;

	g_debug ("eas_mail_handler_watch_email_folders++");

	g_assert (self);
	g_assert (folder_ids);
	g_assert (heartbeat);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	folder_array = g_malloc0 ( (list_length + 1) * sizeof (gchar*));
	if (!folder_array) {
		g_set_error (error, EAS_MAIL_ERROR,
			     EAS_MAIL_ERROR_NOTENOUGHMEMORY,
			     ("out of memory"));
	}

	for (; loop < list_length; ++loop) {
		g_debug ("eas_mail_watch 1");
		folder_array[loop] = g_strdup (g_slist_nth_data ( (GSList*) folder_ids, loop));
		g_debug ("Folder Id: [%s]", folder_array[loop]);
	}


	g_debug ("eas_mail_handler_watch_email_folders1");
	// call dbus api
	dbus_g_proxy_begin_call (proxy, "watch_email_folders",
				 watch_email_folder_completed,
				 cb, 				//callback pointer
				 NULL, 						// destroy notification
				 G_TYPE_STRING, self->priv->account_uid,
				 G_TYPE_STRING, heartbeat,
				 G_TYPE_STRV, folder_array,
				 G_TYPE_INVALID);
	g_strfreev (folder_array);



	if (!ret) {
		g_assert (error == NULL || *error != NULL);
	}
	g_debug ("eas_mail_handler_watch_email_folders--");
	return ret;

}
