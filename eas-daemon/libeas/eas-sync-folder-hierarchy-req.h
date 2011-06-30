/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */

#ifndef _EAS_SYNC_FOLDER_HIERARCHY_REQ_H_
#define _EAS_SYNC_FOLDER_HIERARCHY_REQ_H_

#include <glib-object.h>
#include "eas-request-base.h"

G_BEGIN_DECLS

#define EAS_TYPE_SYNC_FOLDER_HIERARCHY_REQ             (eas_sync_folder_hierarchy_req_get_type ())
#define EAS_SYNC_FOLDER_HIERARCHY_REQ(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), EAS_TYPE_SYNC_FOLDER_HIERARCHY_REQ, EasSyncFolderHierarchyReq))
#define EAS_SYNC_FOLDER_HIERARCHY_REQ_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), EAS_TYPE_SYNC_FOLDER_HIERARCHY_REQ, EasSyncFolderHierarchyReqClass))
#define EAS_IS_SYNC_FOLDER_HIERARCHY_REQ(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EAS_TYPE_SYNC_FOLDER_HIERARCHY_REQ))
#define EAS_IS_SYNC_FOLDER_HIERARCHY_REQ_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), EAS_TYPE_SYNC_FOLDER_HIERARCHY_REQ))
#define EAS_SYNC_FOLDER_HIERARCHY_REQ_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), EAS_TYPE_SYNC_FOLDER_HIERARCHY_REQ, EasSyncFolderHierarchyReqClass))

typedef struct _EasSyncFolderHierarchyReqClass EasSyncFolderHierarchyReqClass;
typedef struct _EasSyncFolderHierarchyReq EasSyncFolderHierarchyReq;
typedef struct _EasSyncFolderHierarchyReqPrivate EasSyncFolderHierarchyReqPrivate;

struct _EasSyncFolderHierarchyReqClass
{
	EasRequestBaseClass parent_class;
};

struct _EasSyncFolderHierarchyReq
{
	EasRequestBase parent_instance;

	EasSyncFolderHierarchyReqPrivate* priv;
};

GType eas_sync_folder_hierarchy_req_get_type (void) G_GNUC_CONST;

/** 
 * Create a new sync folder hierarchy request GObject
 *
 * @param[in] syncKey
 *	  Current sync key.
 * @param[in] accountId
 *	  Unique identifier for a user account.
 * @param[in] context
 *	  DBus context token.
 *
 * @return An allocated EasSyncFolderHierarchyReq GObject or NULL
 */
EasSyncFolderHierarchyReq* eas_sync_folder_hierarchy_req_new (const gchar* syncKey,
                                                             const gchar* accountId,
                                                             DBusGMethodInvocation* context);

/**
 * Builds the messages required for the request and sends the request to the server.
 *
 * @param[in] self
 *	  The EasSyncFolderHierarchyReq GObject instance to be Activated.
 * @param[out] error
 *	  GError may be NULL if the caller wishes to ignore error details, otherwise
 *	  will be populated with error details if the function returns FALSE. Caller
 *	  should free the memory with g_error_free() if it has been set. [full transfer]
 *
 * @return TRUE if successful, otherwise FALSE.
 */
gboolean 
eas_sync_folder_hierarchy_req_Activate (EasSyncFolderHierarchyReq* self, 
                                        GError** error);

/**
 * Called from the Soup thread when we have the final response from the server.
 *
 * Responsible for parsing the server response with the help of the message and
 * then returning the results across the dbus to the client 
 *
 * @param[in] self
 *	  The EasSyncFolderHierarchyReq GObject instance whose messages are complete.
 * @param[in] doc
 *    Document tree containing the server's response. This must be freed using
 *	  xmlFreeDoc(). [full transfer]
 * @param[in] error
 *	  A GError code that has been propagated from the server response.
 *
 * @return TRUE if finished and needs unreffing, FALSE otherwise.
 */
gboolean 
eas_sync_folder_hierarchy_req_MessageComplete (EasSyncFolderHierarchyReq* self, 
                                               xmlDoc *doc, 
                                               GError* error_in);


G_END_DECLS

#endif /* _EAS_SYNC_FOLDER_HIERARCHY_REQ_H_ */
