/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * code
 * Copyright (C)  2011 <>
 * 
 * code is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * code is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "eas-utils.h"
#include "eas-sync-msg.h"
#include "eas-update-email-req.h"

G_DEFINE_TYPE (EasUpdateEmailReq, eas_update_email_req, EAS_TYPE_REQUEST_BASE);

#define EAS_UPDATE_EMAIL_REQ_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EAS_TYPE_UPDATE_EMAIL_REQ, EasUpdateEmailReqPrivate))

struct _EasUpdateEmailReqPrivate
{
	EasSyncMsg* sync_msg;
	guint64 account_id;
	gchar* sync_key;
	gchar* folder_id;
	gchar** serialised_email_array; 
};

static void
eas_update_email_req_init (EasUpdateEmailReq *object)
{
	g_debug("eas_update_email_req_init++");
	/* initialization code */
	EasUpdateEmailReqPrivate *priv;
	
	object->priv = priv = EAS_UPDATE_EMAIL_REQ_PRIVATE(object);

	priv->sync_msg = NULL;
	priv->account_id = 0;
	priv->sync_key = NULL;
	priv->folder_id = NULL;
	priv->serialised_email_array = NULL;

	eas_request_base_SetRequestType (&object->parent_instance, 
	                                 EAS_REQ_UPDATE_MAIL);

	g_debug("eas_update_email_req_init++");

	return;
	
}

static void
eas_update_email_req_finalize (GObject *object)
{
	g_debug("eas_update_email_req_finalize++");
	/* deinitalization code */
	EasUpdateEmailReq *req = (EasUpdateEmailReq *) object;
	EasUpdateEmailReqPrivate *priv = req->priv;

	g_object_unref(priv->sync_msg);
	free_string_array(priv->serialised_email_array);
	g_free (priv);
	req->priv = NULL;

	G_OBJECT_CLASS (eas_update_email_req_parent_class)->finalize (object);

	g_debug("eas_update_email_req_finalize--");	
}

static void
eas_update_email_req_class_init (EasUpdateEmailReqClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	EasRequestBaseClass* parent_class = EAS_REQUEST_BASE_CLASS (klass);

	// get rid of warnings about above 2 lines
	void *temp = (void*)object_class;
	temp = (void*)parent_class;

	g_type_class_add_private (klass, sizeof (EasUpdateEmailReqPrivate));	
	
	object_class->finalize = eas_update_email_req_finalize;

	g_debug("eas_update_email_req_class_init--");
}

// TODO - update this to take a GSList of serialised emails? rem to copy the list
EasUpdateEmailReq *eas_update_email_req_new(guint64 account_id, const gchar *sync_key, const gchar *folder_id, const gchar **serialised_email_array, EFlag *flag)
{
	g_debug("eas_update_email_req_new++");

	EasUpdateEmailReq* self = g_object_new (EAS_TYPE_UPDATE_EMAIL_REQ, NULL);
	EasUpdateEmailReqPrivate *priv = self->priv;
	
	g_assert(sync_key);
	g_assert(folder_id);
	g_assert(serialised_email_array);

	guint num_serialised_emails = array_length(serialised_email_array);
	priv->sync_key = g_strdup(sync_key);
	priv->folder_id = g_strdup(folder_id);
	// TODO duplicate the string array
	priv->serialised_email_array = g_malloc0((num_serialised_emails * sizeof(gchar*)) + 1); // allow for null terminate
	if(!priv->serialised_email_array)
	{
		goto cleanup;
	}
	guint i;
	for(i = 0; i < num_serialised_emails; i++)
	{
		priv->serialised_email_array[i] = g_strdup(serialised_email_array[i]);
	}
	priv->serialised_email_array[i] = NULL;
	
	priv->account_id = account_id;

	eas_request_base_SetFlag(&self->parent_instance, flag);

cleanup:
	if(!priv->serialised_email_array)
	{
		g_warning("Failed to allocate memory!");
		g_free(priv->sync_key);
		g_free(priv->folder_id);
		if(self)
		{
			g_object_unref(self);
			self = NULL;
		}
	}
	
	g_debug("eas_update_email_req_new--");
	return self;	
}

void eas_update_email_req_Activate(EasUpdateEmailReq *self, GError** error)
{
	g_debug("eas_update_email_req_Activate++");
	
	EasUpdateEmailReqPrivate *priv = self->priv;
	xmlDoc *doc;
	GSList *update_emails = NULL;   // sync msg expects a list, we have an array
	guint i = 0;
	while(priv->serialised_email_array[i])
	{
		g_debug("append email to list");
		update_emails = g_slist_append(update_emails, priv->serialised_email_array[i]);
		i++;
	}
	
	//create sync msg object
	priv->sync_msg = eas_sync_msg_new (priv->sync_key, priv->account_id, priv->folder_id, EAS_ITEM_MAIL);

	g_debug("build messsage");
	//build request msg
	doc = eas_sync_msg_build_message (priv->sync_msg, FALSE, NULL, update_emails, NULL);
	
	g_debug("send message");
	eas_connection_send_request(eas_request_base_GetConnection (&self->parent_instance), "Sync", doc, self);

	g_debug("eas_update_email_req_Activate--");		

	g_slist_free(update_emails);

	return;
}


void eas_update_email_req_MessageComplete(EasUpdateEmailReq *self, xmlDoc* doc, GError** error)
{
	g_debug("eas_update_email_req_MessageComplete++");	

	EasUpdateEmailReqPrivate *priv = self->priv;

	eas_sync_msg_parse_reponse (priv->sync_msg, doc, error);

	xmlFree(doc);
	
	e_flag_set(eas_request_base_GetFlag (&self->parent_instance));

	g_debug("eas_update_email_req_MessageComplete--");	
}

/*
void eas_update_email_req_ActivateFinish (EasUpdateEmailReq* self, gchar** ret_sync_key)
{
	g_debug("eas_update_email_req_ActivateFinish++");
	
	EasUpdateEmailReqPrivate *priv = self->priv;

	*ret_sync_key = g_strdup(eas_sync_msg_get_syncKey(priv->sync_msg));
	
	g_debug("eas_update_email_req_ActivateFinish--");	
}
*/
