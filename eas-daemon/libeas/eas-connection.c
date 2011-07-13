/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * ActiveSync core protocol library
 *
 * Copyright © 2011 Intel Corporation.
 *
 * Authors: Mobica Ltd. <www.mobica.com>
 *
 * This file is provided under a dual Apache/LGPLv2.1 licence.  When
 * using or redistributing this file, you may do so under either
 * licence.
 *
 *
 * LGPLv2.1 LICENCE SUMMARY
 *
 *   Copyright © Intel Corporation, dates as above.
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later
 *   version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free
 *   Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *   Boston, MA 02110-1301 USA
 *
 *
 * APACHE LICENCE SUMMARY
 *
 *   Copyright © Intel Corporation, dates as above.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#include "eas-connection.h"
#include <glib.h>
#include <libsoup/soup.h>

#include <wbxml/wbxml.h>
#include <libedataserver/e-flag.h>
#include <libxml/xmlreader.h> // xmlDoc
#include <time.h>
#include <unistd.h>
#include <gnome-keyring.h>

#include <sys/stat.h>

//#include "eas-accounts.h"
#include "../../libeasaccount/src/eas-account-list.h"
#include "eas-connection-errors.h"

// List of includes for each request type
#include "eas-provision-req.h"

#include "../src/activesyncd-common-defs.h"
#include "../src/eas-mail.h"

#ifdef ACTIVESYNC_14
#define AS_DEFAULT_PROTOCOL 140
#else
#define AS_DEFAULT_PROTOCOL 121
#endif

struct _EasConnectionPrivate
{
    SoupSession* soup_session;
    GThread* soup_thread;
    GMainLoop* soup_loop;
    GMainContext* soup_context;

    gchar* accountUid;
    EasAccount *account;

	gboolean retrying_asked;

    const gchar* request_cmd;
    xmlDoc* request_doc;
    struct _EasRequestBase* request;
    GError **request_error;

	int protocol_version;
	gchar *proto_str;
};

#define EAS_CONNECTION_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EAS_TYPE_CONNECTION, EasConnectionPrivate))

typedef struct _EasGnomeKeyringResponse
{
	GnomeKeyringResult result;
	gchar* password;
	EFlag *semaphore;
} EasGnomeKeyringResponse;

static GStaticMutex connection_list = G_STATIC_MUTEX_INIT;
static GHashTable *g_open_connections = NULL;
static GConfClient* g_gconf_client = NULL;
static EasAccountList* g_account_list = NULL;
static GSList* g_mock_response_list = NULL;

static void connection_authenticate (SoupSession *sess, SoupMessage *msg,
                                     SoupAuth *auth, gboolean retrying,
                                     gpointer data);
static gpointer eas_soup_thread (gpointer user_data);
static void handle_server_response (SoupSession *session, SoupMessage *msg, gpointer data);
static gboolean wbxml2xml (const WB_UTINY *wbxml, const WB_LONG wbxml_len, WB_UTINY **xml, WB_ULONG *xml_len);
static void parse_for_status (xmlNode *node, gboolean *isErrorStatus);
static gchar *device_type, *device_id;

static void write_response_to_file (const WB_UTINY* xml, WB_ULONG xml_len);


G_DEFINE_TYPE (EasConnection, eas_connection, G_TYPE_OBJECT);

#define HTTP_STATUS_OK (200)
#define HTTP_STATUS_PROVISION (449)

static void
eas_connection_accounts_init()
{
	g_debug("eas_connection_accounts_init++");

	if (!g_gconf_client)
	{
		// At this point we don't have an account Id so just load the list of accounts
		g_gconf_client = gconf_client_get_default();
		if (g_gconf_client == NULL) 
		{
			g_critical("Error Failed to create GConfClient");
			return;
		}
		g_debug("-->created gconf_client");
		
		g_account_list = eas_account_list_new (g_gconf_client);
		if (g_account_list == NULL) 
		{
			g_critical("Error Failed to create account list ");
			return;
		}
		g_debug("-->created account_list");

		// Find the DeviceType and DeviceId from GConf, or create them
		// if they don't already exist
		device_type = gconf_client_get_string (g_gconf_client,
											   "/apps/activesyncd/device_type",
											   NULL);
		if (!device_type) {
			device_type = g_strdup("MeeGo");
			gconf_client_set_string (g_gconf_client,
									  "/apps/activesyncd/device_type",
									  device_type, NULL);
		}

		device_id = gconf_client_get_string (g_gconf_client,
											 "/apps/activesyncd/device_id",
											 NULL);
		if (!device_id) {
			device_id = g_strdup_printf ("%08x%08x%08x%08x",
										 g_random_int(), g_random_int(),
										 g_random_int(), g_random_int());
			gconf_client_set_string (g_gconf_client,
									  "/apps/activesyncd/device_id",
									  device_id, NULL);
		}
		g_debug ("device type %s, device id %s", device_type, device_id);
	}
	g_debug("eas_connection_accounts_init--");
}

static void
eas_connection_init (EasConnection *self)
{
    EasConnectionPrivate *priv;
    self->priv = priv = EAS_CONNECTION_PRIVATE (self);

    g_debug ("eas_connection_init++");

    priv->soup_context = g_main_context_new ();
    priv->soup_loop = g_main_loop_new (priv->soup_context, FALSE);

    priv->soup_thread = g_thread_create (eas_soup_thread, priv, TRUE, NULL);

    /* create the SoupSession for this connection */
    priv->soup_session =
        soup_session_async_new_with_options (SOUP_SESSION_USE_NTLM,
                                             TRUE,
                                             SOUP_SESSION_ASYNC_CONTEXT,
                                             priv->soup_context,
                                             SOUP_SESSION_TIMEOUT,
                                             120,
                                             NULL);

    g_signal_connect (priv->soup_session,
                      "authenticate",
                      G_CALLBACK (connection_authenticate),
                      self);

    if (getenv ("EAS_SOUP_LOGGER") && (atoi (g_getenv ("EAS_SOUP_LOGGER")) >= 1))
    {
        SoupLogger *logger;
        logger = soup_logger_new (SOUP_LOGGER_LOG_HEADERS, -1);
        soup_session_add_feature (priv->soup_session, SOUP_SESSION_FEATURE (logger));
    }

    priv->accountUid = NULL;
	priv->account = NULL; // Just a reference
	priv->protocol_version = AS_DEFAULT_PROTOCOL;
	priv->proto_str = NULL;
    priv->request_cmd = NULL;
    priv->request_doc = NULL;
    priv->request = NULL;
    priv->request_error = NULL;

	priv->retrying_asked = FALSE;

    g_debug ("eas_connection_init--");
}

static void
eas_connection_dispose (GObject *object)
{
    EasConnection *cnc = (EasConnection *) object;
    EasConnectionPrivate *priv = NULL;
    gchar* hashkey = NULL;

	g_debug("eas_connection_dispose++");
    g_return_if_fail (EAS_IS_CONNECTION (cnc));

    priv = cnc->priv;

    if (g_open_connections)
    {
        hashkey = g_strdup_printf ("%s@%s", 
                                   eas_account_get_username(priv->account), 
                                   eas_account_get_uri(priv->account));
        g_hash_table_remove (g_open_connections, hashkey);
        g_free (hashkey);
        if (g_hash_table_size (g_open_connections) == 0)
        {
            g_hash_table_destroy (g_open_connections);
            g_open_connections = NULL;
        }
    }

    g_signal_handlers_disconnect_by_func (priv->soup_session,
                                          connection_authenticate,
                                          cnc);

    if (priv->soup_session)
    {
        g_object_unref (priv->soup_session);
        priv->soup_session = NULL;

        g_main_loop_quit (priv->soup_loop);
        g_thread_join (priv->soup_thread);
        priv->soup_thread = NULL;

        g_main_loop_unref (priv->soup_loop);
        priv->soup_loop = NULL;
        g_main_context_unref (priv->soup_context);
        priv->soup_context = NULL;
    }

    if (priv->request)
    {
        // TODO - @@WARNING Check this is a valid thing to do.
        // It might only call the base gobject class finalize method not the
        // correct method. Not sure if gobjects are properly polymorphic.
        g_object_unref (priv->request);
		priv->request = NULL;
    }

    G_OBJECT_CLASS (eas_connection_parent_class)->dispose (object);

	g_debug("eas_connection_dispose--");
}

static void
eas_connection_finalize (GObject *object)
{
	EasConnection *cnc = (EasConnection *) object;
    EasConnectionPrivate *priv = cnc->priv;

    g_debug ("eas_connection_finalize++");

    g_free (priv->accountUid);

    if (priv->request_doc)
    {
		if (priv->protocol_version < 140) {
			if (!g_strcmp0 ("SendMail", priv->request_cmd))
			{
				g_free ((gchar*)priv->request_doc);
			}
			else
			{
				xmlFreeDoc (priv->request_doc);
			}
		} else {
			xmlFreeDoc (priv->request_doc);
		}
		priv->request_doc = NULL;
    }
	
    priv->request_cmd = NULL;

    if (priv->request_error && *priv->request_error)
    {
        g_error_free (*priv->request_error);
    }

	g_free (priv->proto_str);

    G_OBJECT_CLASS (eas_connection_parent_class)->finalize (object);
    g_debug ("eas_connection_finalize--");
}

static void
eas_connection_class_init (EasConnectionClass *klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS (klass);
    g_debug ("eas_connection_class_init++");

    g_type_class_add_private (klass, sizeof (EasConnectionPrivate));

    object_class->dispose = eas_connection_dispose;
    object_class->finalize = eas_connection_finalize;

    eas_connection_accounts_init();

    g_debug ("eas_connection_class_init--");
}

static void
storePasswordCallback (GnomeKeyringResult result, gpointer data)
{
	EasGnomeKeyringResponse *response = data;

	g_debug("storePasswordCallback++");
	
	g_assert (response);
	g_assert (response->semaphore);
	g_assert (!response->password);
	
	response->result = result;
	e_flag_set (response->semaphore);
	g_debug("storePasswordCallback--");
}

static GnomeKeyringResult 
writePasswordToKeyring (const gchar *password, const gchar* username, const gchar* serverUri)
{
	GnomeKeyringResult result = GNOME_KEYRING_RESULT_DENIED;
	EasGnomeKeyringResponse *response = g_malloc0(sizeof(EasGnomeKeyringResponse));
	gchar * description = g_strdup_printf ("Exchange Server Password for %s@%s", 
	                                       username,
	                                       serverUri);

	g_debug("writePasswordToKeyring++");
	response->semaphore = e_flag_new ();
	g_assert (response->semaphore);
	g_debug("writePasswordToKeyring++ 1");
	gnome_keyring_store_password (GNOME_KEYRING_NETWORK_PASSWORD,
                                  NULL,
                                  description,
                                  password,
                                  storePasswordCallback,
                                  response,
                                  NULL,
                                  "user", username,
                                  "server", serverUri,
                                  NULL);
	g_debug("writePasswordToKeyring++ 2");
	g_free (description);
	e_flag_wait(response->semaphore);
	g_debug("writePasswordToKeyring++ 3");
	e_flag_free (response->semaphore);
	g_debug("writePasswordToKeyring++ 4");
	result = response->result;
	g_free (response);

	g_debug("writePasswordToKeyring--");
	return result;
}

static void
getPasswordCallback (GnomeKeyringResult result, const char* password, gpointer data)
{
	EasGnomeKeyringResponse *response = data;

	g_debug("getPasswordCallback++");

	g_assert (response);
	g_assert (response->semaphore);

	response->result = result;
	response->password = g_strdup (password);
	e_flag_set (response->semaphore);
	
	g_debug("getPasswordCallback--");
}

static gboolean
fetch_and_store_password (const gchar* username, const gchar * serverUri)
{
	gchar *argv[] = {(gchar*)ASKPASS, (gchar*)"Please enter your Exchange Password", NULL};
	gchar * password = NULL;
	GError * error = NULL;
	GnomeKeyringResult result = GNOME_KEYRING_RESULT_DENIED;
	gint exit_status = 0;

	g_debug("fetch_and_store_password++");

	if (FALSE == g_spawn_sync (NULL, argv, NULL,
	                           G_SPAWN_SEARCH_PATH,
	                           NULL, NULL,
	                           &password, NULL, 
	                           &exit_status,
	                           &error))
	{
		g_warning ("Failed to spawn : [%d][%s]", error->code, error->message);
		g_error_free (error);
		return FALSE;
	}

	g_strchomp (password);

	result = writePasswordToKeyring (password, username, serverUri);
	
	memset (password, 0, strlen(password));
	g_free(password);
	password = NULL;

	g_debug("fetch_and_store_password--");
	return (result == GNOME_KEYRING_RESULT_OK);
}

static GnomeKeyringResult 
getPasswordFromKeyring (const gchar* username, const gchar* serverUri, char** password)
{
	GnomeKeyringResult result = GNOME_KEYRING_RESULT_DENIED;
	EFlag *semaphore = NULL;
	EasGnomeKeyringResponse *response = g_malloc0(sizeof(EasGnomeKeyringResponse));

	g_debug("getPasswordFromKeyring++");

	g_assert (password && *password == NULL);

	response->semaphore = semaphore = e_flag_new ();

	gnome_keyring_find_password (GNOME_KEYRING_NETWORK_PASSWORD,
	                             getPasswordCallback,
	                             response,
	                             NULL,
	                             "user", username,
	                             "server", serverUri,
	                             NULL);

	e_flag_wait (response->semaphore);
	e_flag_free (response->semaphore);
	result = response->result;
	*password = response->password;
	g_free (response);

	g_debug("getPasswordFromKeyring--");
	return result;
}

static void 
connection_authenticate (SoupSession *sess, 
                         SoupMessage *msg, 
                         SoupAuth *auth, 
                         gboolean retrying, 
                         gpointer data)
{
    EasConnection* cnc = (EasConnection *) data;
	const gchar * username = eas_account_get_username(cnc->priv->account);
	const gchar * serverUri = eas_account_get_uri(cnc->priv->account);
	GnomeKeyringResult result = GNOME_KEYRING_RESULT_DENIED;
	gchar* password = NULL;

    g_debug ("  eas_connection - connection_authenticate++");

	// @@FIX ME - Temporary grab of password from GConf

	password = eas_account_get_password (cnc->priv->account);
	if (password)
	{
		g_warning("Found password in GConf, writting it to Gnome Keyring");

		if (GNOME_KEYRING_RESULT_OK != writePasswordToKeyring(password, username, serverUri))
		{
			g_warning("Failed to store GConf password in Gnome Keyring");
		}
	}

	password = NULL;

	result = getPasswordFromKeyring (username, serverUri, &password);

	if (GNOME_KEYRING_RESULT_NO_MATCH == result)
	{
		g_warning("Failed to find password in Gnome Keyring");

		if (fetch_and_store_password(username, serverUri))
		{
			if (GNOME_KEYRING_RESULT_OK == getPasswordFromKeyring (username, serverUri, &password))
			{
				g_debug("First authentication attempt with newly set password");
				cnc->priv->retrying_asked = FALSE;
				soup_auth_authenticate (auth, 
						                username,
						                password);
			}
			else 
			{
				g_critical ("Failed to fetch and store password to gnome keyring [%d]", result);
			}
			
			if (password)
			{
				memset (password, 0 , strlen(password));
				g_free (password);
				password = NULL;
			}
		}
	}
	else if (result != GNOME_KEYRING_RESULT_OK)
	{
		g_warning("GnomeKeyring failed to find password [%d]", result);
	}
	else
	{
		g_debug ("Found password in Gnome Keyring");
		if (!retrying)
		{
			g_debug ("First authentication attempt");

			cnc->priv->retrying_asked = FALSE;

			soup_auth_authenticate (auth, 
				                    username,
				                    password);
			if (password)
			{
				memset (password, 0 , strlen(password));
				g_free (password);
				password = NULL;
			}
		}
		else if (!cnc->priv->retrying_asked)
		{
			g_debug ("Second authentication attempt - original password was incorrect");
			cnc->priv->retrying_asked = TRUE;
			
			if (password)
			{
				memset (password, 0 , strlen(password));
				g_free (password);
				password = NULL;
			}

			if (fetch_and_store_password(username, serverUri))
			{
				if (GNOME_KEYRING_RESULT_OK == getPasswordFromKeyring (username, serverUri, &password))
				{
					g_debug ("Second authentication with newly set password");
					soup_auth_authenticate (auth, 
					                        username,
					                        password);
				}
				else 
				{
					g_critical ("Failed to store password to gnome keyring [%d]", result);
				}
				
				if (password)
				{
					memset (password, 0 , strlen(password));
					g_free (password);
					password = NULL;
				}
			}
		}
		else
		{
			g_debug("Failed too many times, authentication aborting");
		}
	}

	g_debug ("  eas_connection - connection_authenticate--");
}

static gpointer
eas_soup_thread (gpointer user_data)
{
    EasConnectionPrivate *priv = user_data;

    g_debug ("  eas_connection - eas_soup_thread++");

    g_main_context_push_thread_default (priv->soup_context);
    g_main_loop_run (priv->soup_loop);
    g_main_context_pop_thread_default (priv->soup_context);

    g_debug ("  eas_connection - eas_soup_thread--");
    return NULL;
}

void eas_connection_set_policy_key (EasConnection* self, const gchar* policyKey)
{
    EasConnectionPrivate *priv = self->priv;

    g_debug ("eas_connection_set_policy_key++");

	eas_account_set_policy_key (priv->account, policyKey);
	
	eas_account_list_save_item(g_account_list,
							   priv->account,
							   EAS_ACCOUNT_POLICY_KEY);
							   
    g_debug ("eas_connection_set_policy_key--");
}

EasAccount *
eas_connection_get_account (EasConnection *self)
{
    EasConnectionPrivate *priv = self->priv;

	return priv->account;
}

int
eas_connection_get_protocol_version (EasConnection *self)
{
    EasConnectionPrivate *priv = self->priv;

	return priv->protocol_version;
}

void eas_connection_resume_request (EasConnection* self, gboolean provisionSuccessful)
{
    EasConnectionPrivate *priv = self->priv;

    g_debug ("eas_connection_resume_request++");

	if (!priv->request_cmd && !priv->request && 
	    !priv->request_doc && !priv->request_error)
	{
		g_warning("Attempting to resume when no request stored, have we double provisioned?");
	}
	else
	{
		if (provisionSuccessful)
		{
			const gchar *_cmd = priv->request_cmd;
			struct _EasRequestBase *_request = priv->request;
			xmlDoc *_doc = priv->request_doc;
			GError **_error = priv->request_error;

			priv->request_cmd   = NULL;
			priv->request       = NULL;
			priv->request_doc   = NULL;
			priv->request_error = NULL;

			g_debug ("Provisioning was successful - resending original request");
		
			eas_connection_send_request (self, _cmd, _doc, _request, _error);
		}
		else
		{
			g_debug ("Provisioning failed - cleaning up original request");

			// Clean up request data
			if (priv->protocol_version < 140 &&
				!g_strcmp0("SendMail", priv->request_cmd))
			{
				g_free((gchar*)priv->request_doc);
			}
			else
			{
				xmlFreeDoc (priv->request_doc);
			}

			priv->request_cmd   = NULL;
			priv->request       = NULL;
			priv->request_doc   = NULL;
			priv->request_error = NULL;
		}
	}

    g_debug ("eas_connection_resume_request--");
}

static gboolean
eas_queue_soup_message (gpointer _request)
{
	EasRequestBase *request = EAS_REQUEST_BASE (_request);
	EasConnection *self = eas_request_base_GetConnection (request);
	SoupMessage *msg = eas_request_base_GetSoupMessage (request);
	EasConnectionPrivate *priv = self->priv;

    soup_session_queue_message(priv->soup_session, 
                               msg, 
                               handle_server_response, 
                               request);

	return FALSE;
}

/*
emits a signal (if the dbus interface object has been set)

*/
static void emit_signal (SoupBuffer *chunk, EasRequestBase *request)
{
	guint request_id = eas_request_base_GetRequestId (request);
	EasMail* dbus_interface;
	EasMailClass* dbus_interface_klass;
	guint percent, total, so_far;

	dbus_interface = eas_request_base_GetInterfaceObject(request); 
	if(dbus_interface)
	{
		dbus_interface_klass = EAS_MAIL_GET_CLASS (dbus_interface);	

		total = eas_request_base_GetDataSize(request);

		eas_request_base_UpdateDataLengthSoFar(request, chunk->length);
		so_far = eas_request_base_GetDataLengthSoFar(request);

		// TODO what should we send if percentage not possible?
		if(total)
		{
			percent = so_far * 100 / total;

			g_debug("emit signal with percent: %d * 100 / %d = %d", so_far, total, percent);
			
			//emit signal
			g_signal_emit (dbus_interface,
			dbus_interface_klass->signal_id,
			0,
			request_id,
			percent);	
		}
	}	
}

static void soap_got_chunk (SoupMessage *msg, SoupBuffer *chunk, gpointer data)
{
	EasRequestBase *request	 = (EasRequestBase *) data;
	gboolean outgoing_progress = eas_request_base_GetRequestProgressDirection(request);
	
	g_debug("soap_got_chunk");
	
	if (msg->status_code != 200)
		return;

	if(!outgoing_progress)// want incoming progress updates
	{
		emit_signal(chunk, request);
	}
	
	g_debug ("Received %zd bytes for request %p", chunk->length, request);
}

static void soap_got_headers (SoupMessage *msg, gpointer data)
{
	struct _EasRequestBase *request = data;
	const gchar *size_hdr;
	gboolean outgoing_progress = eas_request_base_GetRequestProgressDirection(request);
	
	g_debug("soap_got_headers");
	size_hdr = soup_message_headers_get_one (msg->response_headers,
											 "Content-Length");
	if (size_hdr) {
		gsize size = strtoull (size_hdr, NULL, 10);
		// store the response size in the request base
		if(!outgoing_progress)  // want incoming progress, so store size
		{
			eas_request_base_SetDataSize(request, size);
		}
		
		g_debug ("Response size of request %p is %zu bytes", request, size);
		// We can stash this away and use it to provide progress updates
		// as a percentage. If we don't have a Content-Length: header,
		// for example if the server uses chunked encoding, then we cannot
		// do percentages.
	} else {
		if(!outgoing_progress)
		{
			eas_request_base_SetDataSize(request, 0);
		}
		g_debug ("Response size of request %p is unknown", request);
		// Note that our got_headers signal handler may be called more than
		// once for a given request, if it fails once with 'authentication
		// needed' then succeeds on being resubmitted. So if the *first*
		// response has a Content-Length but the second one doesn't, we have
		// to do the right thing and not keep using the first length.
	}
}


static void 
soap_wrote_body_data (SoupMessage *msg, SoupBuffer *chunk, gpointer data)
{
	struct _EasRequestBase *request = data;
	gboolean outgoing_progress = eas_request_base_GetRequestProgressDirection(request);
	
	g_debug("soap_wrote_body_data %d", chunk->length);

	if(outgoing_progress)
	{
		emit_signal(chunk, request);
	}
	
	g_debug ("Wrote %zd bytes for request %p", chunk->length, request);
}

static void soap_wrote_headers (SoupMessage *msg, gpointer data)
{
	struct _EasRequestBase *request = data;
	const gchar *size_hdr;
	gboolean outgoing_progress = eas_request_base_GetRequestProgressDirection(request);
	
	g_debug("soap_wrote_headers");

	if(outgoing_progress)
	{
		size_hdr = soup_message_headers_get_one (msg->request_headers,
												 "Content-Length");	
		if (size_hdr) 
		{	
			gsize size = strtoull (size_hdr, NULL, 10);

			// store the request size in the request base
			if(outgoing_progress)
			{
				eas_request_base_SetDataSize(request, size);
				eas_request_base_SetDataLengthSoFar(request, 0);	// reset 
			}
		
			g_debug ("Request size of request %p is %zu bytes", request, size);

		} 
		else 
		{
			eas_request_base_SetDataSize(request, 0);
			g_debug ("Request size of request %p is unknown", request);
		}		
	}
}

/**
 * WBXML encode the message and send to exchange server via libsoup.
 * May also be required to temporarily hold the request message whilst
 * provisioning with the server occurs.
 *
 * @param self
 *	  The EasConnection GObject instance.
 * @param cmd 
 *	  ActiveSync command string [no transfer]
 * @param doc
 *	  The message xml body [full transfer]
 * @param request
 *	  The request GObject
 * @param[out] error
 *	  GError may be NULL if the caller wishes to ignore error details, otherwise
 *	  will be populated with error details if the function returns FALSE. Caller 
 *	  should free the memory with g_error_free() if it has been set. [full transfer]
 *
 * @return TRUE if successful, otherwise FALSE.
 */
gboolean
eas_connection_send_request (EasConnection* self, 
                             const gchar* cmd, 
                             xmlDoc* doc, 
                             EasRequestBase *request, 
                             GError** error)
{
    gboolean ret = TRUE;
    EasConnectionPrivate *priv = self->priv;
    SoupMessage *msg = NULL;
    gchar* uri = NULL;
    WB_UTINY *wbxml = NULL;
    WB_ULONG wbxml_len = 0;
    WBXMLError wbxml_ret = WBXML_OK;
    WBXMLConvXML2WBXML *conv = NULL;
    xmlChar* dataptr = NULL;
    int data_len = 0;
	GSource *source = NULL;
	const gchar *policy_key;

    g_debug ("eas_connection_send_request++");
    // If not the provision request, store the request
    if (g_strcmp0 (cmd, "Provision"))
    {
		gint recursive = 1;
		g_debug("store the request");
        priv->request_cmd = g_strdup (cmd);

		if(priv->protocol_version < 140 &&
		   !g_strcmp0(cmd, "SendMail"))
		{
			priv->request_doc = (gpointer)g_strdup((gchar*) doc);
		}
		else
		{
			priv->request_doc = xmlCopyDoc (doc, recursive);
		}

        priv->request = request;
        priv->request_error = error;
    }

	policy_key = eas_account_get_policy_key (priv->account);
    // If we need to provision, and not the provisioning msg
    if ( (!policy_key || !g_strcmp0("0",policy_key)) && g_strcmp0 (cmd, "Provision"))
    {
        EasProvisionReq *req = eas_provision_req_new (NULL, NULL);
		EasRequestBase *req_base = EAS_REQUEST_BASE (&req->parent_instance);
        g_debug ("  eas_connection_send_request - Provisioning required");

        eas_request_base_SetConnection (&req->parent_instance, self);
		// For provisioning copy the DBus Context of the original request.
		eas_request_base_SetContext (req_base,
		                             eas_request_base_GetContext (request));

        ret = eas_provision_req_Activate (req, error);
        if (!ret)
        {
            g_assert (error == NULL || *error != NULL);
        }
        goto finish;
    }

    wbxml_ret = wbxml_conv_xml2wbxml_create (&conv);
    if (wbxml_ret != WBXML_OK)
    {
        g_set_error (error, EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_WBXMLERROR,
                     ("error %d returned from wbxml_conv_xml2wbxml_create"), wbxml_ret);
        ret = FALSE;
        goto finish;
    }

    uri = g_strconcat (eas_account_get_uri(priv->account),
                       "?Cmd=", cmd,
                       "&User=", eas_account_get_username(priv->account),
                       "&DeviceId=", device_id,
                       "&DeviceType=", device_type,
                       NULL);

    msg = soup_message_new ("POST", uri);
    g_free (uri);
    if (!msg)
    {
        g_set_error (error, EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_SOUPERROR,
                     ("soup_message_new returned NULL"));
        ret = FALSE;
        goto finish;
    }

    soup_message_headers_append (msg->request_headers,
                                 "MS-ASProtocolVersion",
                                priv->proto_str);

    soup_message_headers_append (msg->request_headers,
                                 "User-Agent",
                                 "libeas");

    soup_message_headers_append (msg->request_headers,
                                 "X-MS-PolicyKey",
                                 policy_key?:"0");
//in activesync 12.1, SendMail uses mime, not wbxml in the body
if( priv->protocol_version >= 140 || g_strcmp0(cmd, "SendMail"))
{
	// Convert doc into a flat xml string
    xmlDocDumpFormatMemoryEnc (doc, &dataptr, &data_len, (gchar*) "utf-8", 1);
    wbxml_conv_xml2wbxml_disable_public_id (conv);
    wbxml_conv_xml2wbxml_disable_string_table (conv);
    wbxml_ret = wbxml_conv_xml2wbxml_run (conv, dataptr, data_len, &wbxml, &wbxml_len);

    if (getenv ("EAS_DEBUG") && (atoi (g_getenv ("EAS_DEBUG")) >= 5))
    {
        gchar* tmp = g_strndup ( (gchar*) dataptr, data_len);
        g_debug ("\n=== XML Input ===\n%s=== XML Input ===", tmp);
        g_free (tmp);

        g_debug ("wbxml_conv_xml2wbxml_run [Ret:%s],  wbxml_len = [%d]", wbxml_errors_string (wbxml_ret), wbxml_len);
    }

    wbxml_ret = wbxml_conv_xml2wbxml_run (conv, dataptr, data_len, &wbxml, &wbxml_len);
    if (wbxml_ret != WBXML_OK)
    {
        g_set_error (error, EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_WBXMLERROR,
                     ("error %d returned from wbxml_conv_xml2wbxml_run"), wbxml_ret);
        ret = FALSE;
        goto finish;
    }

    soup_message_headers_set_content_length (msg->request_headers, wbxml_len);

    soup_message_set_request (msg,
                              "application/vnd.ms-sync.wbxml",
                              SOUP_MEMORY_COPY,
                              (gchar*) wbxml,
                              wbxml_len);
}
else
{
    //Activesync 12.1 implementation for SendMail Command
    soup_message_headers_set_content_length (msg->request_headers, strlen((gchar*)doc));

    soup_message_set_request (msg,
                              "message/rfc822",
                              SOUP_MEMORY_COPY,
                              (gchar*) doc,
                              strlen((gchar*)doc));

	//Avoid double-free in handle_server_response()
	priv->request_doc = NULL;
}

    eas_request_base_SetSoupMessage (request, msg);

    g_signal_connect (msg, "got-chunk", G_CALLBACK (soap_got_chunk), request);
    g_signal_connect (msg, "got-headers", G_CALLBACK (soap_got_headers), request);
    g_signal_connect (msg, "wrote-body-data", G_CALLBACK (soap_wrote_body_data), request);
    g_signal_connect (msg, "wrote-headers", G_CALLBACK (soap_wrote_headers), request);
    // We have to call soup_session_queue_message() from the soup thread,
    // or libsoup screws up (https://bugzilla.gnome.org/642573)
	source = g_idle_source_new ();
	g_source_set_callback (source, eas_queue_soup_message, request, NULL);
	g_source_attach (source, priv->soup_context);

finish:
	// @@WARNING - doc must always be freed before exiting this function.

if(priv->protocol_version < 140 && !g_strcmp0(cmd, "SendMail"))
	{
		g_free ((gchar*)doc);
	}
	else
	{
		xmlFreeDoc(doc);
	}

    if (wbxml) free (wbxml);
    if (conv) wbxml_conv_xml2wbxml_destroy (conv);
    if (dataptr) xmlFree (dataptr);

    if (!ret)
    {
        g_assert (error == NULL || *error != NULL);
    }
    g_debug ("eas_connection_send_request--");
    return ret;
}

typedef enum
{
    INVALID = 0,
    VALID_NON_EMPTY,
    VALID_EMPTY,
    VALID_12_1_REPROVISION,
} RequestValidity;


static RequestValidity
isResponseValid (SoupMessage *msg, GError **error)
{
    const gchar *content_type = NULL;

    g_debug ("eas_connection - isResponseValid++");
    
    if (HTTP_STATUS_PROVISION == msg->status_code)
    {
		g_warning("Server instructed 12.1 style re-provision");
		return VALID_12_1_REPROVISION;
	}

    if (HTTP_STATUS_OK != msg->status_code)
    {
        g_critical ("Failed with status [%d] : %s", msg->status_code, (msg->reason_phrase ? msg->reason_phrase : "-"));
        g_set_error (error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "HTTP request failed: %d - %s",
					 msg->status_code, msg->reason_phrase);
        return INVALID;
    }

    if (!msg->response_body->length)
    {
        g_debug ("Empty Content");
        return VALID_EMPTY;
    }

    content_type = soup_message_headers_get_one (msg->response_headers, "Content-Type");

    if (0 != g_strcmp0 ("application/vnd.ms-sync.wbxml", content_type))
    {
        g_warning ("  Failed: Content-Type did not match WBXML");
		g_set_error (error,
					 EAS_CONNECTION_ERROR,
					 EAS_CONNECTION_ERROR_FAILED,
					 "HTTP response type was not WBXML");
        return INVALID;
    }

    g_debug ("eas_connection - isResponseValid--");

    return VALID_NON_EMPTY;
}


/**
 * Converts from Microsoft Exchange Server protocol WBXML to XML
 * @param wbxml input buffer
 * @param wbxml_len input buffer length
 * @param xml output buffer [full transfer]
 * @param xml_len length of the output buffer in bytes
 */
static gboolean
wbxml2xml (const WB_UTINY *wbxml, const WB_LONG wbxml_len, WB_UTINY **xml, WB_ULONG *xml_len)
{
    gboolean ret = TRUE;
    WBXMLConvWBXML2XML *conv = NULL;
    WBXMLError wbxml_ret = WBXML_OK;

    g_debug ("eas_connection - wbxml2xml++");

    *xml = NULL;
    *xml_len = 0;

    if (NULL == wbxml)
    {
        g_warning ("  wbxml is NULL!");
        ret = FALSE;
        goto finish;
    }

    if (0 == wbxml_len)
    {
        g_warning ("  wbxml_len is 0!");
        ret = FALSE;
        goto finish;
    }

    wbxml_ret = wbxml_conv_wbxml2xml_create (&conv);

    if (wbxml_ret != WBXML_OK)
    {
        g_warning ("  Failed to create conv! %s", wbxml_errors_string (ret));
        ret = FALSE;
        goto finish;
    }

    wbxml_conv_wbxml2xml_set_language (conv, WBXML_LANG_ACTIVESYNC);
    wbxml_conv_wbxml2xml_set_indent (conv, 2);

    wbxml_ret = wbxml_conv_wbxml2xml_run (conv,
                                          (WB_UTINY *) wbxml,
                                          wbxml_len,
                                          xml,
                                          xml_len);

    if (WBXML_OK != wbxml_ret)
    {
        g_warning ("  wbxml2xml Ret = %s", wbxml_errors_string (wbxml_ret));
        ret = FALSE;
    }

finish:
    if (conv) wbxml_conv_wbxml2xml_destroy (conv);

    return ret;
}

/**
 * @param wbxml binary XML to be dumped
 * @param wbxml_len length of the binary
 */
static void
dump_wbxml_response (const WB_UTINY *wbxml, const WB_LONG wbxml_len)
{
    WB_UTINY *xml = NULL;
    WB_ULONG xml_len = 0;
    gchar* tmp = NULL;

	if (0 != wbxml_len)
	{
		wbxml2xml (wbxml, wbxml_len, &xml, &xml_len);
		tmp = g_strndup ( (gchar*) xml, xml_len);
		g_debug ("\n=== dump start: xml_len [%d] ===\n%s=== dump end ===", xml_len, tmp);
		if (tmp) g_free (tmp);
		if (xml) free (xml);
	}
	else
	{
		g_warning("No WBXML to decode");
	}
}

////////////////////////////////////////////////////////////////////////////////
//
// Autodiscover
//
////////////////////////////////////////////////////////////////////////////////

static xmlDoc *
autodiscover_as_xml (const gchar *email)
{
    xmlDoc *doc;
    xmlNode *node, *child;
    xmlNs *ns;

    doc = xmlNewDoc ( (xmlChar *) "1.0");
    node = xmlNewDocNode (doc, NULL, (xmlChar *) "Autodiscover", NULL);
    xmlDocSetRootElement (doc, node);
    ns = xmlNewNs (node, (xmlChar *) "http://schemas.microsoft.com/exchange/autodiscover/mobilesync/requestschema/2006", NULL);
    node = xmlNewChild (node, ns, (xmlChar *) "Request", NULL);
    child = xmlNewChild (node, ns, (xmlChar *) "EMailAddress", (xmlChar *) email);
    child = xmlNewChild (node, ns, (xmlChar *) "AcceptableResponseSchema",
                         (xmlChar *) "http://schemas.microsoft.com/exchange/autodiscover/mobilesync/responseschema/2006");

    return doc;
}

/**
 * @return NULL or serverUri as a NULL terminated string, caller must free 
 *		   with xmlFree(). [full transfer]
 *
 */
static gchar*
autodiscover_parse_protocol (xmlNode *node)
{
    for (node = node->children; node; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE &&
                !g_strcmp0 ( (char *) node->name, "Url"))
        {
            char *asurl = (char *) xmlNodeGetContent (node);
            if (asurl)
                return asurl;
        }
    }
    return NULL;
}

typedef struct
{
    EasConnection *cnc;
    GSimpleAsyncResult *simple;
    SoupMessage *msgs[2];
    EasAutoDiscoverCallback cb;
    gpointer cbdata;
	EasAccount* account;
} EasAutoDiscoverData;

static void
autodiscover_soup_cb (SoupSession *session, SoupMessage *msg, gpointer data)
{
    GError *error = NULL;
    EasAutoDiscoverData *adData = data;
    guint status = msg->status_code;
    xmlDoc *doc = NULL;
    xmlNode *node = NULL;
    gchar *serverUrl = NULL;
    gint idx = 0;

    g_debug ("autodiscover_soup_cb++");

    for (; idx < 2; ++idx)
    {
        if (adData->msgs[idx] == msg)
            break;
    }

    if (idx == 2)
    {
        g_debug ("Request got cancelled and removed");
        return;
    }

    adData->msgs[idx] = NULL;
	
    if (status != HTTP_STATUS_OK)
    {
        g_warning ("Autodiscover HTTP response was not OK");
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "Status code: %d - Response from server",
                     status);
        goto failed;
    }

    doc = xmlReadMemory (msg->response_body->data,
                         msg->response_body->length,
                         "autodiscover.xml",
                         NULL,
                         XML_PARSE_NOERROR | XML_PARSE_NOWARNING);

    if (!doc)
    {
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "Failed to parse autodiscover response XML");
        goto failed;
    }

    node = xmlDocGetRootElement (doc);
    if (g_strcmp0 ( (gchar*) node->name, "Autodiscover"))
    {
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "Failed to find <Autodiscover> element");
        goto failed;
    }
    for (node = node->children; node; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE && !g_strcmp0 ( (gchar*) node->name, "Response"))
            break;
    }
    if (!node)
    {
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "Failed to find <Response> element");
        goto failed;
    }
    for (node = node->children; node; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE && !g_strcmp0 ( (gchar*) node->name, "Action"))
            break;
    }
    if (!node)
    {
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "Failed to find <Action> element");
        goto failed;
    }
    for (node = node->children; node; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE && !g_strcmp0 ( (gchar*) node->name, "Settings"))
            break;
    }
    if (!node)
    {
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "Failed to find <Settings> element");
        goto failed;
    }
    for (node = node->children; node; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE &&
                !g_strcmp0 ( (gchar*) node->name, "Server") &&
                (serverUrl = autodiscover_parse_protocol (node)))
            break;
    }

    for (idx = 0; idx < 2; ++idx)
    {
        if (adData->msgs[idx])
        {
            SoupMessage *m = adData->msgs[idx];
            adData->msgs[idx] = NULL;
            soup_session_cancel_message (adData->cnc->priv->soup_session,
                                         m,
                                         SOUP_STATUS_CANCELLED);
            g_debug ("Autodiscover success - Cancelling outstanding msg[%d]", idx);
        }
    }
	// Copy the pointer here so we can free using g_free() rather than xmlFree()
    g_simple_async_result_set_op_res_gpointer (adData->simple, g_strdup (serverUrl), NULL);
	if (serverUrl) xmlFree (serverUrl);
    g_simple_async_result_complete_in_idle (adData->simple);

    g_debug ("autodiscover_soup_cb (Success)--");
    return;

failed:
    for (idx = 0; idx < 2; ++idx)
    {
        if (adData->msgs[idx])
        {
            /* Clear this error and hope the second one succeeds */
            g_warning ("First autodiscover attempt failed");
            g_clear_error (&error);
            return;
        }
    }

    /* This is returning the last error, it may be preferable for the
     * first error to be returned.
     */
    g_simple_async_result_set_from_error (adData->simple, error);
    g_simple_async_result_complete_in_idle (adData->simple);

    g_debug ("autodiscover_soup_cb (Failed)--");
}

static void
autodiscover_simple_cb (GObject *cnc, GAsyncResult *res, gpointer data)
{
    EasAutoDiscoverData *adData = data;
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
    GError *error = NULL;
    gchar *url = NULL;

    g_debug ("autodiscover_simple_cb++");

    if (!g_simple_async_result_propagate_error (simple, &error))
    {
        url = g_simple_async_result_get_op_res_gpointer (simple);
    }

	// Url ownership transferred to cb
    adData->cb (url, adData->cbdata, error);
    g_object_unref (G_OBJECT (adData->cnc));
	g_object_unref (G_OBJECT (adData->account));
    g_free (adData);

    g_debug ("autodiscover_simple_cb--");
}


static SoupMessage *
autodiscover_as_soup_msg (gchar *url, xmlOutputBuffer *buf)
{
    SoupMessage *msg = NULL;

    g_debug ("autodiscover_as_soup_msg++");

    msg = soup_message_new ("POST", url);

    soup_message_headers_append (msg->request_headers,
                                 "User-Agent", "libeas");

    soup_message_set_request (msg,
                              "text/xml",
                              SOUP_MEMORY_COPY,
                              (gchar*) buf->buffer->content,
                              buf->buffer->use);

    g_debug ("autodiscover_as_soup_msg--");
    return msg;
}

////////////////////////////////////////////////////////////////////////////////
//
// Public functions
//
////////////////////////////////////////////////////////////////////////////////

/**
 * @param[in] cb
 *	  Autodiscover response callback
 * @param[in] cb_data
 *	  Data to be passed into the callback
 * @param[in] email
 *	  User's exchange email address
 * @param[in] username
 *	  Exchange Server username in the format DOMAIN\username
 */
void
eas_connection_autodiscover (EasAutoDiscoverCallback cb,
                             gpointer cb_data,
                             const gchar* email,
                             const gchar* username)
{
    GError *error = NULL;
    gchar* domain = NULL;
    EasConnection *cnc = NULL;
    xmlDoc *doc = NULL;
    xmlOutputBuffer* txBuf = NULL;
    gchar* url = NULL;
    EasAutoDiscoverData *autoDiscData = NULL;
	EasAccount *account = NULL;

    g_debug ("eas_connection_autodiscover++");

    g_type_init();
    dbus_g_thread_init();

    if (!email)
    {
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "Email is mandatory and must be provided");
        cb (NULL, cb_data, error);
        return;
    }

    domain = strchr (email, '@');
    if (! (domain && *domain))
    {
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_FAILED,
                     "Failed to extract domain from email address");
        cb (NULL, cb_data, error);
        return;
    }
    ++domain; // Advance past the '@'

	account = eas_account_new();
	if (!account)
	{
        g_set_error (&error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_NOTENOUGHMEMORY,
                     "Failed create temp account for autodiscover");
        cb (NULL, cb_data, error);
        return;
	}
#if 0
	account->uid = g_strdup (email);
	account->serverUri = g_strdup("autodiscover");
#endif

	eas_account_set_uid(account, email);	
	eas_account_set_uri(account, "autodiscover");

    if (!username) // Use the front of the email as the username
    {
        gchar **split = g_strsplit (email, "@", 2);
		eas_account_set_username(account, split[0]);
        g_strfreev (split);
    }
    else // Use the supplied username
    {
        eas_account_set_username(account, username);
    }

    cnc = eas_connection_new (account, &error);

    if (!cnc)
    {
        cb (NULL, cb_data, error);
		g_object_unref (account);
        return;
    }

    doc = autodiscover_as_xml (email);
    txBuf = xmlAllocOutputBuffer (NULL);
    xmlNodeDumpOutput (txBuf, doc, xmlDocGetRootElement (doc), 0, 1, NULL);
    xmlOutputBufferFlush (txBuf);

    autoDiscData = g_new0 (EasAutoDiscoverData, 1);
    autoDiscData->cb = cb;
    autoDiscData->cbdata = cb_data;
    autoDiscData->cnc = cnc;
	autoDiscData->account = account;
    autoDiscData->simple = g_simple_async_result_new (G_OBJECT (cnc),
                                                      autodiscover_simple_cb,
                                                      autoDiscData,
                                                      eas_connection_autodiscover);

    // URL formats to be tried
    g_debug ("Building message one");
    // 1 - https://<domain>/autodiscover/autodiscover.xml
    url = g_strdup_printf ("https://%s/autodiscover/autodiscover.xml", domain);
    autoDiscData->msgs[0] = autodiscover_as_soup_msg (url, txBuf);
    g_free (url);

    g_debug ("Building message two");
    // 2 - https://autodiscover.<domain>/autodiscover/autodiscover.xml
    url = g_strdup_printf ("https://autodiscover.%s/autodiscover/autodiscover.xml", domain);
    autoDiscData->msgs[1] = autodiscover_as_soup_msg (url, txBuf);
    g_free (url);

    soup_session_queue_message (cnc->priv->soup_session,
                                autoDiscData->msgs[0],
                                autodiscover_soup_cb,
                                autoDiscData);

    soup_session_queue_message (cnc->priv->soup_session,
                                autoDiscData->msgs[1],
                                autodiscover_soup_cb,
                                autoDiscData);

    g_object_unref (cnc); // GSimpleAsyncResult holds this now
    xmlOutputBufferClose (txBuf);
    xmlFreeDoc (doc);

    g_debug ("eas_connection_autodiscover--");
}

EasConnection*
eas_connection_find (const gchar* accountId)
{
    EasConnection *cnc = NULL;
    GError *error = NULL;
	EIterator *iter = NULL;
	gboolean account_found = FALSE;
	EasAccount* account = NULL;

    g_debug ("eas_connection_find++ : account_uid[%s]",
             (accountId ? accountId : "NULL"));

    g_type_init();
    dbus_g_thread_init();

    if (!accountId) return NULL;

    eas_connection_accounts_init();

	iter = e_list_get_iterator (E_LIST ( g_account_list));
	for (; e_iterator_is_valid (iter);  e_iterator_next (iter))
	{
		account = EAS_ACCOUNT (e_iterator_get (iter));
		g_print("account->uid=%s\n", eas_account_get_uid(account));
		if (strcmp (eas_account_get_uid(account), accountId) == 0) {
			account_found = TRUE;
			break;
		}
	}

	if(!account_found)
	{
		g_warning("No account details found for accountId [%s]", accountId);
		return NULL;
	}
	
    g_static_mutex_lock (&connection_list);
    if (g_open_connections)
    {
		gchar *hashkey = g_strdup_printf("%s@%s", 
                                         eas_account_get_username(account), 
                                         eas_account_get_uri(account));

        cnc = g_hash_table_lookup (g_open_connections, hashkey);
        g_free (hashkey);

        if (EAS_IS_CONNECTION (cnc))
        {
            g_object_ref (cnc);
            g_static_mutex_unlock (&connection_list);
            g_debug ("eas_connection_find (Found) --");
            return cnc;
        }
    }
    g_static_mutex_unlock (&connection_list);

	cnc = eas_connection_new (account, &error);
    if (cnc)
    {
        g_debug ("eas_connection_find (Created) --");
    }
    else
    {
        if (error)
        {
            g_warning ("[%s][%d][%s]",
                       g_quark_to_string (error->domain),
                       error->code,
                       error->message);
            g_error_free (error);
        }
        g_warning ("eas_connection_find (Failed to create connection) --");
    }

    return cnc;
}


EasConnection*
eas_connection_new (EasAccount* account, GError** error)
{
    EasConnection *cnc = NULL;
    EasConnectionPrivate *priv = NULL;
    gchar *hashkey = NULL;

    g_debug ("eas_connection_new++");

    g_type_init();
    dbus_g_thread_init();

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

    if (!account)
    {
        g_set_error (error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_BADARG,
                     "An account must be provided.");
        return NULL;
    }

    g_debug ("Checking for open connection");
    g_static_mutex_lock (&connection_list);
    if (g_open_connections)
    {
        hashkey = g_strdup_printf ("%s@%s", eas_account_get_username(account), eas_account_get_uri(account));
        cnc = g_hash_table_lookup (g_open_connections, hashkey);
        g_free (hashkey);

        if (EAS_IS_CONNECTION (cnc))
        {
            g_object_ref (cnc);
            g_static_mutex_unlock (&connection_list);
            return cnc;
        }
    }

    g_debug ("No existing connection, create new one");
    cnc = g_object_new (EAS_TYPE_CONNECTION, NULL);
    priv = cnc->priv;

    if (!cnc)
    {
        g_set_error (error,
                     EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_NOTENOUGHMEMORY,
                     "A server url and username must be provided.");
        g_static_mutex_unlock (&connection_list);
        return NULL;
    }

	priv->protocol_version = eas_account_get_protocol_version (account);
	if (!priv->protocol_version)
		priv->protocol_version = AS_DEFAULT_PROTOCOL;
	priv->proto_str = g_strdup_printf ("%d.%d", priv->protocol_version / 10,
									   priv->protocol_version % 10);

	// Just a reference to the global account list
	priv->account = account;

    hashkey = g_strdup_printf ("%s@%s", eas_account_get_username(account), eas_account_get_uri(account));

    if (!g_open_connections)
    {
        g_debug ("Creating hashtable");
        g_open_connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    }

    g_debug ("Adding to hashtable");
    g_hash_table_insert (g_open_connections, hashkey, cnc);

    g_static_mutex_unlock (&connection_list);
    g_debug ("eas_connection_new--");
    return cnc;
}

static void
parse_for_status (xmlNode *node, gboolean *isErrorStatus)
{
    xmlNode *child = NULL;
    xmlNode *sibling = NULL;

	*isErrorStatus = FALSE;

    if (!node) return;
    if ( (child = node->children))
    {
        parse_for_status (child, isErrorStatus);
    }

    if (*isErrorStatus == FALSE && node->type == XML_ELEMENT_NODE && !g_strcmp0 ( (char *) node->name, "Status"))
    {
        gchar* status = (gchar*) xmlNodeGetContent (node);
        if (!g_strcmp0 (status, "1") || (!g_strcmp0 (status, "3") && !g_strcmp0 ((gchar *)node->parent->name, "Response")) )
        {
            g_message ("parent_name[%s] status = [%s]", (char*) node->parent->name , status);
        }
        else
        {
            g_critical ("parent_name[%s] status = [%s]", (char*) node->parent->name , status);
            *isErrorStatus = TRUE;
        }
        xmlFree (status);
    }

    if ( (sibling = node->next) && *isErrorStatus == FALSE)
    {
        parse_for_status (sibling, isErrorStatus);
    }
}

void
handle_server_response (SoupSession *session, SoupMessage *msg, gpointer data)
{
    EasRequestBase *req = EAS_REQUEST_BASE (data);
    EasConnection *self = EAS_CONNECTION (eas_request_base_GetConnection (req));
    EasConnectionPrivate *priv = self->priv;
    xmlDoc *doc = NULL;
    WB_UTINY *xml = NULL;
    WB_ULONG xml_len = 0;
    gboolean isProvisioningRequired = FALSE;
    GError *error = NULL;
    RequestValidity validity = FALSE; 
	gboolean cleanupRequest = FALSE;

    g_debug ("eas_connection - handle_server_response++ self [%lx], priv[%lx]", 
             (unsigned long)self, (unsigned long)self->priv );

	validity = isResponseValid (msg, &error);

    if (INVALID == validity)
    {
		g_assert (error != NULL);
		write_response_to_file ((WB_UTINY*) msg->response_body->data, msg->response_body->length);
        goto complete_request;
    }

	if (VALID_12_1_REPROVISION != validity)
	{
		if (getenv ("EAS_DEBUG") && (atoi (g_getenv ("EAS_DEBUG")) >= 5))
		{
		    dump_wbxml_response ( (WB_UTINY*) msg->response_body->data, msg->response_body->length);
		}
	}

    if (VALID_NON_EMPTY == validity)
    {
        gboolean isStatusError = FALSE;
        if (!wbxml2xml ( (WB_UTINY*) msg->response_body->data,
                         msg->response_body->length,
                         &xml,
                         &xml_len))
        {
            g_set_error (&error, EAS_CONNECTION_ERROR,
                         EAS_CONNECTION_ERROR_WBXMLERROR,
                         ("Converting wbxml failed"));
            goto complete_request;
        }

        if (getenv ("EAS_CAPTURE_RESPONSE") && (atoi (g_getenv ("EAS_CAPTURE_RESPONSE")) >= 1))
		{
			write_response_to_file (xml, xml_len);
		}

        g_debug ("handle_server_response - pre-xmlReadMemory");

		// @@TRICKY - If we have entries in the mocked body list, 
		//            feed that response back instead of the real server
		//            response.
		if (g_mock_response_list)
		{
			gchar *filename = g_slist_nth_data (g_mock_response_list, 0);
			gchar* fullPath = g_strconcat(g_getenv("HOME"), 
			                              "/eas-test-responses/", 
			                              filename, 
			                              NULL);
			
			g_debug ("Queued mock responses [%u]", g_slist_length (g_mock_response_list));
			
			g_mock_response_list = g_slist_remove (g_mock_response_list, filename);
			g_free (filename);

			g_debug ("Loading mock:[%s]", fullPath);
			
			doc = xmlReadFile (fullPath,
			                   NULL,
			                   0);
			if (!doc)
			{
				g_critical("Failed to read mock response!");
			}
			g_free (fullPath);
		}
		else
		{
			// Otherwise proccess the server response
		    doc = xmlReadMemory ( (const char*) xml,
		                          xml_len,
		                          "sync.xml",
		                          NULL,
		                          0);
		}

        if (doc)
        {
            xmlNode* node = xmlDocGetRootElement (doc);
            parse_for_status (node, &isStatusError); // TODO Also catch provisioning for 14.0
        }

        if (TRUE == isStatusError || !doc)
        {
            if (!getenv ("EAS_CAPTURE_RESPONSE") || (getenv ("EAS_CAPTURE_RESPONSE") && (atoi (g_getenv ("EAS_CAPTURE_RESPONSE")) == 0)))
			{
        		write_response_to_file (xml, xml_len);
			}
        }

        if (xml) free (xml);
    }

	if (VALID_12_1_REPROVISION == validity)
	{
		isProvisioningRequired = TRUE;
	}

complete_request:
    if (!isProvisioningRequired)
    {
        EasRequestType request_type = eas_request_base_GetRequestType (req);

        g_debug ("  handle_server_response - no parsed provisioning required");
        g_debug ("  handle_server_response - Handling request [%d]", request_type);

        if (request_type != EAS_REQ_PROVISION)
        {
            // Clean up request data
			if (priv->protocol_version < 140 && !g_strcmp0("SendMail", priv->request_cmd))
			{
				g_free((gchar*)priv->request_doc);
			}
			else
			{
        		xmlFreeDoc (priv->request_doc);
			}

            priv->request_cmd = NULL;
            priv->request_doc = NULL;
            priv->request = NULL;
            priv->request_error = NULL;
        }

        cleanupRequest = eas_request_base_MessageComplete (req, doc, error);

        //if cleanupRequest is set - we are done with this request, and should clean it up
        if(cleanupRequest)
        {
            g_object_unref(req);
        }
    }
    else
    {
        EasProvisionReq *prov_req = eas_provision_req_new (NULL, NULL);
        g_debug ("  handle_server_response - parsed provisioning required");

        eas_request_base_SetConnection (&prov_req->parent_instance, self);
        // Don't delete this request and create a provisioning request.
        eas_provision_req_Activate (prov_req, &error);   // TODO check return
    }
    g_debug ("eas_connection - handle_server_response--");
}

static void
write_response_to_file (const WB_UTINY* xml, WB_ULONG xml_len)
{
	static gulong sequence_number = 0;
	FILE *file = NULL;
	gchar *path = g_strdup_printf("%s/eas-debug/", g_getenv("HOME"));
	gchar *filename = NULL;
	gchar *fullPath = NULL; 

	if (!sequence_number)
	{
		mkdir(path, S_IFDIR | S_IRWXU | S_IXGRP | S_IXOTH);
	}
	sequence_number++;

	filename = g_strdup_printf("%010lu.%lu.xml", (gulong) sequence_number, (gulong) getpid ());
	fullPath = g_strconcat(path, filename, NULL);

	if ( (file = fopen(fullPath, "w")) )
	{
		fwrite (xml, 1, xml_len, file);
		fclose (file);
	}

	g_free(path);
	g_free(filename);
	g_free(fullPath);
}

void
eas_connection_add_mock_responses (const gchar** response_file_list)
{
	const gchar* filename = NULL;
	guint index = 0;

	g_debug ("eas_connection_add_mock_responses++");
	
	if (!response_file_list) return;

	while ( (filename = response_file_list[index++]) )
	{
		g_debug("Adding [%s] to the mock list", filename);
		g_mock_response_list = g_slist_append (g_mock_response_list, g_strdup(filename));
	}

	g_debug ("eas_connection_add_mock_responses--");
}
