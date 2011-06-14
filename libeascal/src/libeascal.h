/*
 *  Filename: libeascal.h
 */

#ifndef EAS_CAL_H
#define EAS_CAL_H

#include <glib-object.h>


G_BEGIN_DECLS

#define EAS_TYPE_CAL_HANDLER             (eas_cal_handler_get_type ())
#define EAS_CAL_HANDLER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), EAS_TYPE_CAL_HANDLER, EasCalHandler))
#define EAS_CAL_HANDLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), EAS_TYPE_EMAIL_HANDLER, EasCalHandlerClass))
#define EAS_IS_CAL_HANDLER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EAS_TYPE_CAL_HANDLER))
#define EAS_IS_CAL_HANDLER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), EAS_TYPE_CAL_HANDLER))
#define EAS_CAL_HANDLER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), EAS_TYPE_CAL_HANDLER, EasCalHandlerClass))

typedef struct _EasCalHandlerClass EasCalHandlerClass;
typedef struct _EasCalHandler EasCalHandler;
typedef struct _EasCalHandlerPrivate EasCalHandlerPrivate;

struct _EasCalHandlerClass{
	GObjectClass parent_class;
};

struct _EasCalHandler{
	GObject parent_instance;
	EasCalHandlerPrivate *priv;
};

GType eas_cal_handler_get_type (void) G_GNUC_CONST;
// This method is called once by clients of the libeas plugin for each account.  The method
// takes an ID that identifies the account and returns a pointer to an EasCalHandler object.
// This object is required to be passed to all subsiquent calls to this interface to identify the
// account and facilitate the interface to the daemon.
// Note: the account_uid is not validated against accounts provisioned on the device as part of 
// this call.  This level of validation will be done on subsequent calls that take EasCalHandler
// as an argument
EasCalHandler *eas_cal_handler_new(const char* account_uid);

/* function name:               eas_cal_handler_get calendar_items
 * function description:        pulls down changes in calendar folder
 * return value:                TRUE if function success, FALSE if error
 * params: 
 * EasCalHandler* this (in):  use value returned from eas_cal_hander_new()
 * gchar *sync_key (in / out):  use zero for initial hierarchy or saved value returned 
 *                              from exchange server for subsequent sync requests
 * GSList **items_created (out): returns a list of EasCalInfo structs that describe
 *                              created items.  If there are no new created items
 *                              this parameter will be unchanged.
 * GSList **items_updated (out): returns a list of EasCalInfo structs that describe
 *                              updated items.  If there are no new updated items
 *                              this parameter will be unchanged.
 * GSList **items_deleted (out): returns a list of strings that show server IDs of 
 *                              deleted items.  If there are no new deleted items
 *                              this parameter will be unchanged.
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/
gboolean eas_cal_handler_get_calendar_items(EasCalHandler* this, 
                                                 gchar *sync_key, 
                                                 GSList **items_created,	
                                                 GSList **items_updated,
                                                 GSList **items_deleted,
                                                 GError **error);
                                                 
/* function name:               eas_cal_handler_delete_items
 * function description:        delete items in calendar folder
 * return value:                TRUE if function success, FALSE if error
 * params: 
 * EasCalHandler* this (in):  use value returned from eas_cal_hander_new()
 * gchar *sync_key (in / out):  use zero for initial hierarchy or saved value returned 
 *                              from exchange server for subsequent sync requests
 * GSList *items_deleted (in): provides a list of strings that identify the deleted
 *                              items' server IDs. 
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/
gboolean eas_cal_handler_delete_items(EasCalHandler* this, 
                                                 gchar *sync_key, 
                                                 GSList *items_deleted,
                                                 GError **error);

/* function name:               eas_cal_handler_update_items
 * function description:        update items in calendar folder
 * return value:                TRUE if function success, FALSE if error
 * params: 
 * EasCalHandler* this (in):  use value returned from eas_cal_hander_new()
 * gchar *sync_key (in / out):  use zero for initial hierarchy or saved value returned 
 *                              from exchange server for subsequent sync requests
 * GSList *items_updated (in): provides a list of EasCalInfo structs that describe
 *                              update items.  If there are no new updated items
 *                              this parameter will be unchanged.
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/
gboolean eas_cal_handler_update_items(EasCalHandler* this, 
                                                 gchar *sync_key, 
                                                 GSList *items_updated,
                                                 GError **error);

/* function name:               eas_cal_handler_add_items
 * function description:        add items in calendar folder
 * return value:                TRUE if function success, FALSE if error
 * params: 
 * EasCalHandler* this (in):  use value returned from eas_cal_hander_new()
 * gchar *sync_key (in / out):  use zero for initial hierarchy or saved value returned 
 *                              from exchange server for subsequent sync requests
 * GSList *items_added (in): provides a list of EasCalInfo structs that describe
 *                              added items.  If there are no new updated items
 *                              this parameter will be unchanged.
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/
gboolean eas_cal_handler_add_items(EasCalHandler* this, 
                                                 gchar *sync_key, 
                                                 GSList *items_added,
                                                 GError **error);


#endif
