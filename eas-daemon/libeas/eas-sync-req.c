/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */

#include "eas-sync-req.h"
#include "eas-sync-msg.h"
#include "eas-connection-errors.h"
#include "serialise_utils.h"

G_DEFINE_TYPE (EasSyncReq, eas_sync_req, EAS_TYPE_REQUEST_BASE);

#define EAS_SYNC_REQ_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EAS_TYPE_SYNC_REQ, EasSyncReqPrivate))

typedef enum
{
    EasSyncReqStep1 = 0,
    EasSyncReqStep2
} EasSyncReqState;


struct _EasSyncReqPrivate
{
    EasSyncMsg* syncMsg;
    gchar* sync_key;
    EasSyncReqState state;
    gchar* accountID;
    gchar* folderID;
    EasItemType ItemType;
};



static void
eas_sync_req_init (EasSyncReq *object)
{
    EasSyncReqPrivate *priv;

    object->priv = priv = EAS_SYNC_REQ_PRIVATE (object);

    g_debug ("eas_sync_req_init++");
    priv->syncMsg = NULL;
    priv->state = EasSyncReqStep1;
    priv->accountID = NULL;
    priv->folderID = NULL;
	priv->ItemType = EAS_ITEM_NOT_SPECIFIED;
    eas_request_base_SetRequestType (&object->parent_instance,
                                     EAS_REQ_SYNC);

    g_debug ("eas_sync_req_init--");
}

static void
eas_sync_req_dispose (GObject *object)
{
    EasSyncReq *req = (EasSyncReq*) object;
    EasSyncReqPrivate *priv = req->priv;

    g_debug ("eas_sync_req_dispose++");

    if (priv->syncMsg)
    {
        g_object_unref (priv->syncMsg);
    }

	G_OBJECT_CLASS (eas_sync_req_parent_class)->dispose (object);

    g_debug ("eas_sync_req_dispose--");

}

static void
eas_sync_req_finalize (GObject *object)
{
    EasSyncReq *req = (EasSyncReq*) object;
    EasSyncReqPrivate *priv = req->priv;

    g_debug ("eas_sync_req_finalize++");

    g_free (priv->accountID);
    g_free (priv->folderID);

    G_OBJECT_CLASS (eas_sync_req_parent_class)->finalize (object);

    g_debug ("eas_sync_req_finalize--");

}

static void
eas_sync_req_class_init (EasSyncReqClass *klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (EasSyncReqPrivate));

    object_class->finalize = eas_sync_req_finalize;
}

EasSyncReq *eas_sync_req_new (const gchar* syncKey, 
                              const gchar* accountID, 
                              const gchar* folderId, 
                              EasItemType type,
                              DBusGMethodInvocation *context) 
{
    EasSyncReq* self = g_object_new (EAS_TYPE_SYNC_REQ, NULL);
    EasSyncReqPrivate *priv = self->priv;

    g_debug ("eas_sync_req_new++");

	g_assert (syncKey);
	g_assert (accountID);
    g_assert (folderId);
	
    priv->sync_key = g_strdup (syncKey);
    priv->accountID = g_strdup (accountID);
    priv->folderID = g_strdup (folderId);
    priv->ItemType = type;

    eas_request_base_SetContext (&self->parent_instance, context);

    g_debug ("eas_sync_req_new--");
    return self;
}

gboolean
eas_sync_req_Activate (EasSyncReq *self,
                       GError** error)
{
    gboolean ret = FALSE;
    EasSyncReqPrivate* priv;
    xmlDoc *doc;
    gboolean getChanges = FALSE;

    g_debug ("eas_sync_req_activate++");

    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    priv = self->priv;

    g_debug ("eas_sync_req_activate - new Sync  mesg");

	//create sync  msg type
    priv->syncMsg = eas_sync_msg_new (priv->sync_key, priv->accountID, priv->folderID, priv->ItemType);
    if (!priv->syncMsg)
    {
        ret = FALSE;
        // set the error
        g_set_error (error, EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_NOTENOUGHMEMORY,
                     ("out of memory"));
        goto finish;
    }

    g_debug ("eas_sync_req_activate- syncKey = %s", priv->sync_key);

    //if syncKey is not 0, then we are not doing a first time sync and only need to send one message
    // so we  move state machine forward
    if (g_strcmp0 (priv->sync_key, "0"))
    {
        g_debug ("switching state");
        priv->state = EasSyncReqStep2;
        getChanges = TRUE;
    }

    g_debug ("eas_sync_req_activate - build messsage");
    //build request msg
    doc = eas_sync_msg_build_message (priv->syncMsg, getChanges, NULL, NULL, NULL);
    if (!doc)
    {
        ret = FALSE;
		
		// set the error
        g_set_error (error, EAS_CONNECTION_ERROR,
                     EAS_CONNECTION_ERROR_NOTENOUGHMEMORY,
                     ("out of memory"));
        goto finish;
    }

    g_debug ("eas_sync_req_activate - send message");
    ret = eas_connection_send_request (eas_request_base_GetConnection (&self->parent_instance),
                                       "Sync",
                                       doc,
                                       (struct _EasRequestBase *) self,
                                       error);

finish:
    if (!ret)
    {
        g_assert (error == NULL || *error != NULL);

        if (priv->syncMsg)
		{
			g_object_unref (priv->syncMsg);
			priv->syncMsg = NULL;
		}
    }
	
    g_debug ("eas_sync_req_activate--");

    return ret;
}

gboolean
eas_sync_req_MessageComplete (EasSyncReq *self, xmlDoc* doc, GError* error_in)
{
    gboolean cleanup = FALSE;
    gboolean ret = TRUE;
    GError *error = NULL;
    EasSyncReqPrivate* priv = self->priv;
	gchar *ret_sync_key = NULL;
    gboolean ret_more_available = FALSE;
    gchar** ret_added_item_array = NULL;
    gchar** ret_deleted_item_array = NULL;
    gchar** ret_changed_item_array = NULL;

	GSList* added_items = NULL;
    GSList* updated_items = NULL;
    GSList* deleted_items = NULL;

    g_debug ("eas_sync_req_MessageComplete++");

    // if an error occurred, store it and signal daemon
    if (error_in)
    {
		xmlFreeDoc (doc);
		ret = FALSE;
        error = error_in;
        goto finish;
    }

    ret = eas_sync_msg_parse_response (priv->syncMsg, doc, &error);
    xmlFreeDoc (doc);
    if (!ret)
    {
        g_assert (error != NULL);
        goto finish;
    }

    switch (priv->state)
    {
        default:
        {
			ret = FALSE;
			g_set_error (&error, EAS_CONNECTION_ERROR,
				         EAS_CONNECTION_SYNC_ERROR_INVALIDSTATE,
				         ("Invalid state"));
        }
        break;

        //We have started a first time sync, and need to get the sync Key from the result, and then do the proper sync
        case EasSyncReqStep1:
        {
            //get syncKey
            gchar* syncKey = g_strdup (eas_sync_msg_get_syncKey (priv->syncMsg));

            g_debug ("eas_sync_req synckey = %s", syncKey);

            //clean up old message
            if (priv->syncMsg)
            {
                g_object_unref (priv->syncMsg);
            }

            //create new message with new syncKey
            priv->syncMsg = eas_sync_msg_new (syncKey, priv->accountID, priv->folderID, priv->ItemType);
			g_free(syncKey);
            //build request msg
            doc = eas_sync_msg_build_message (priv->syncMsg, TRUE, NULL, NULL, NULL);
			if (!doc)
			{
				g_set_error (&error, EAS_CONNECTION_ERROR,
				             EAS_CONNECTION_ERROR_NOTENOUGHMEMORY,
				             ("out of memory"));
				ret = FALSE;
				goto finish;
			}
            //move to new state
            priv->state = EasSyncReqStep2;

            ret = eas_connection_send_request (eas_request_base_GetConnection (&self->parent_instance),
                                               "Sync",
                                               doc, (struct _EasRequestBase *) self,
                                               &error);
            if (!ret)
            {
                g_assert (error != NULL);
                goto finish;
            }

        }
        break;

        //we did a proper sync, so we need to inform the daemon that we have finished, so that it can continue and get the data
        case EasSyncReqStep2:
        {
			//finished state machine - req needs to be cleanup up by connection object
			cleanup = TRUE;
            g_debug ("eas_sync_req_MessageComplete step 2");
			ret_more_available = eas_sync_msg_get_more_available(priv->syncMsg);
			ret_sync_key  = g_strdup (eas_sync_msg_get_syncKey (priv->syncMsg));
			added_items   = eas_sync_msg_get_added_items (priv->syncMsg);
			updated_items = eas_sync_msg_get_updated_items (priv->syncMsg);
			deleted_items = eas_sync_msg_get_deleted_items (priv->syncMsg);

			switch(priv->ItemType)
			{
				case EAS_ITEM_MAIL:
				{
					ret = build_serialised_email_info_array (&ret_added_item_array, added_items, &error);
					if (ret)
					{
						ret = build_serialised_email_info_array (&ret_changed_item_array, updated_items, &error);
						if (ret)
						{
							ret = build_serialised_email_info_array (&ret_deleted_item_array, deleted_items, &error);
						}
					}
					 if (!ret)
					{
						g_set_error (&error, EAS_CONNECTION_ERROR,
				             EAS_CONNECTION_ERROR_NOTENOUGHMEMORY,
				             ("out of memory"));
						goto finish;
					}
				}
				break;
				case EAS_ITEM_CALENDAR:
				case EAS_ITEM_CONTACT:
				{
					if (build_serialised_calendar_info_array (&ret_added_item_array, added_items, &error))
					{
						if (build_serialised_calendar_info_array (&ret_changed_item_array, updated_items, &error))
						{
							build_serialised_calendar_info_array (&ret_deleted_item_array, deleted_items, &error);
						}
					}
				}
				break;					
				default:
				{
				ret = FALSE;
				g_set_error (&error, EAS_CONNECTION_ERROR,
						     EAS_CONNECTION_SYNC_ERROR_INVALIDTYPE,
						     ("Invalid type"));
				goto finish;
				}
			}
			dbus_g_method_return (eas_request_base_GetContext (&self->parent_instance),
                              ret_sync_key,
                              ret_more_available,
                              ret_added_item_array,
                              ret_deleted_item_array,
                              ret_changed_item_array);

        }
        break;
    }

finish:
	if (!ret)
    {
        g_debug ("returning error %s", error->message);
        g_assert (error != NULL);
        dbus_g_method_return_error (eas_request_base_GetContext (&self->parent_instance), error);
        g_error_free (error);
		error = NULL;
		
		cleanup = TRUE;
    }
	g_free(ret_sync_key);
	g_strfreev(ret_added_item_array);
	g_strfreev(ret_deleted_item_array);
	g_strfreev(ret_changed_item_array);
    g_debug ("eas_sync_req_MessageComplete--");

	return cleanup;
}
