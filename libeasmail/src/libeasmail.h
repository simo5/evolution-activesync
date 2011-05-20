/*
 *  Filename: libeasmail.h
 */

#ifndef EAS_MAIL_H
#define EAS_MAIL_H

#include <glib-object.h>
#include "eas-email-info.h"

G_BEGIN_DECLS

#define EAS_TYPE_EMAIL_HANDLER             (eas_mail_handler_get_type ())
#define EAS_EMAIL_HANDLER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), EAS_TYPE_EMAIL_HANDLER, EasEmailHandler))
#define EAS_EMAIL_HANDLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), EAS_TYPE_EMAIL_HANDLER, EasEmailHandlerClass))
#define EAS_IS_EMAIL_HANDLER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EAS_TYPE_EMAIL_HANDLER))
#define EAS_IS_EMAIL_HANDLER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), EAS_TYPE_EMAIL_HANDLER))
#define EAS_EMAIL_HANDLER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), EAS_TYPE_EMAIL_HANDLER, EasEmailHandlerClass))

typedef struct _EasEmailHandlerClass EasEmailHandlerClass;
typedef struct _EasEmailHandler EasEmailHandler;
typedef struct _EasEmailHandlerPrivate EasEmailHandlerPrivate;

struct _EasEmailHandlerClass{
	GObjectClass parent_class;
};

struct _EasEmailHandler{
	GObject parent_instance;
	EasEmailHandlerPrivate *priv;
};

GType eas_mail_handler_get_type (void) G_GNUC_CONST;
// This method is called once by clients of the libeasmail plugin for each account.  The method
// takes an ID that identifies the account and returns a pointer to an EasEmailHandler object.
// This object is required to be passed to all subsiquent calls to this interface to identify the
// account and facilitate the interface to the daemon.
// Note: the account_uid is not validated against accounts provisioned on the device as part of 
// this call.  This level of validation will be done on subsequent calls that take EasEmailHandler
// as an argument
EasEmailHandler *eas_mail_handler_new(guint64 account_uid);

/* function name:               eas_mail_handler_sync_folder_hierarchy
 * function description:        pulls down changes in folder structure (folders 
 *                              added/deleted/updated). Supplies lists of EasFolders 
 *                              note that each folder has a sync key and the folder 
 *                              *structure* has a separate sync_key
 * return value:                TRUE if function success, FALSE if error
 * params: 
 * EasEmailHandler* this (in):  use value returned from eas_mail_hander_new()
 * gchar *sync_key (in / out):  use zero for initial hierarchy or saved value returned 
 *                              from exchange server for subsequent sync requests
 * GSList **folders_created (out): returns a list of EasFolder structs that describe
 *                              created folders.  If there are no new created folders
 *                              this parameter will be unchanged.
 * GSList **folders_updated (out): returns a list of EasFolder structs that describe
 *                              updated folders.  If there are no new updated folders
 *                              this parameter will be unchanged.
 * GSList **folders_deleted (out): returns a list of EasFolder structs that describe
 *                              deleted folders.  If there are no new deleted folders
 *                              this parameter will be unchanged.
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/
gboolean eas_mail_handler_sync_folder_hierarchy(EasEmailHandler* this, 
                                                 gchar *sync_key, 
                                                 GSList **folders_created,	
                                                 GSList **folders_updated,
                                                 GSList **folders_deleted,
                                                 GError **error);

/* function name:               eas_mail_handler_sync_email_info
 * function description:        sync email changes from the server for a specified folder 
 *                              (no bodies retrieved).  Default Max changes in one sync = 100
 *                              as per Exchange default window size.
 * return value:                TRUE if function success, FALSE if error
 * params: 
 * EasEmailHandler* this (in):  use value returned from eas_mail_hander_new()
 * const gchar *folder_id (in): this value identifies the folder to get the email info from.
 * 								Use the EasFolder->folder_id value in the EasFolder structs
 *                              returned from the eas_mail_handler_sync_folder_hierarchy() call.
 * GSList **emails_created (out): returns a list of EasEmailInfos structs that describe
 *                              created mails.  If there are no new created mails
 *                              this parameter will be unchanged.  In the case of created emails 
 *                              all fields are filled in
 * GSList **emails_updated (out): returns a list of EasEmailInfos structs that describe
 *                              updated mails.  If there are no new updated mails
 *                              this parameter will be unchanged.  In the case of updated emails 
 *                              only the serverids, flags and categories are valid
 * GSList **emails_deleted (out): returns a list of EasEmailInfos structs that describe
 *                              deleted mails.  If there are no new deleted mails
 *                              this parameter will be unchanged.  In the case of deleted emails 
 *                              only the serverids are valid
 * gboolean *more_available:    TRUE if there are more changes to sync (window_size exceeded).   
 *                              Otherwise FALSE.
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/
gboolean eas_mail_handler_sync_folder_email_info(EasEmailHandler* this, 
                                                 gchar *sync_key,
                                                 const gchar *folder_id,	
	                                             GSList **emails_created,
	                                             GSList **emails_updated,	
	                                             GSList **emails_deleted,
	                                             gboolean *more_available,	
	                                             GError **error);


/* function name:               eas_mail_handler_fetch_email_body
 * function description:        get the entire email body for specified email.
 *                              The email body will be written to a file whose name 
 *                              matches the server_id. The directory is expected to exist
 * return value:                TRUE if function success, FALSE if error
 * params:
 * EasEmailHandler* this (in):  use value returned from eas_mail_hander_new()                             
 * const gchar *folder_id (in): this value identifies the folder containing the email to get the body from.
 * 								Use the EasFolder->folder_id value in the EasFolder struct.
 * const gchar *server_id (in): this value identifies the email within the folder (identified by
 *                              folder_id) to get the body from.  Use the EasEmailInfo->server_id
 *                              value in the EasEmailInfo struct returned from the call to
 *                              eas_mail_handler_sync_folder_email_info
 * const gchar *mime_directory (in): directory to put email body into. Mime information will be streamed 
 *                              to a binary file.  Filename will match name supplied in server_id.  
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/

gboolean eas_mail_handler_fetch_email_body(EasEmailHandler* this, 
	 										     const gchar *folder_id, 		                                           
	        									 const gchar *server_id, 		
											     const gchar *mime_directory,
											     GError **error);


/* function name:               eas_mail_handler_fetch_email_attachment
 * function description:        get the entire attachment identified by file_reference
 *                              The attachment is created in the directory supplied in 
 *                              mime_directory
 * return value:                TRUE if function success, FALSE if error
 * params:
 * EasEmailHandler* this (in):  use value returned from eas_mail_hander_new()         
 * const gchar *file_reference (in): this value identifies the attachment to 
 *                              fetch.  Use the EasAttachment->file_reference
 *                              in the EasAttachment 
 * const gchar *mime_directory (in): directory to put attachment into. Filename 
 *                              will match name supplied in file reference
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/ 
gboolean eas_mail_handler_fetch_email_attachment(EasEmailHandler* this, 
                                                const gchar *file_reference, 	
                                                const gchar *mime_directory,	 
                                                GError **error);

/* function name:               eas_mail_handler_delete_email
 * function description:        delete the email described by the EasEmailInfo struct.
 *                              If this method is called on an email not in the "Deleted Items"
 *                              folder then the email is moved to the "Deleted Items" folder.  If
 *                              this method is called on an email in the "Deleted Items" folder
 *                              then the email will be permanently deleted
 * return value:                TRUE if function success, FALSE if error
 * params:
 * EasEmailHandler* this (in):  use value returned from eas_mail_hander_new()
 * gchar *sync_key (in / out):  use value returned from exchange server from previous requests
 * const EasEmailInfo *email (in): identifies the specific email to delete.  This information is 
 *                              returned in a list from the eas_mail_handler_sync_folder_email_info
 *                              call
 * GError **error (out):        returns error information if an error occurs.  If no
 *                              error occurs this will unchanged.  This error information
 *                              could be related to errors in this API or errors propagated
 *                              back through underlying layers
*/
gboolean eas_mail_handler_delete_email(EasEmailHandler* this, 
										gchar *sync_key,                                        
                                        const EasEmailInfo *email,
	                                    GError **error);


/* 
'push' email updates to server
Note that the only valid changes are to the read flag and to categories (other changes ignored)
*/
gboolean eas_mail_handler_update_emails(EasEmailHandler* this, 
                                        gchar *sync_key,
                                        GSList *update_emails,		// List of EasEmailInfos to update
				                        GError **error);


gboolean eas_mail_handler_send_email(EasEmailHandler* this, 
                                    const gchar *client_email_id,	// unique message identifier supplied by client
                                    const gchar *mime_file,			// the full path to the email (mime) to be sent
                                    GError **error);

gboolean eas_mail_handler_move_to_folder(EasEmailHandler* this, 
                                            const GSList *email_ids,
	                                        const gchar *src_folder_id,
	                                        const gchar *dest_folder_id,
	                                        GError **error);

// How supported in AS?
gboolean eas_mail_handler_copy_to_folder(EasEmailHandler* this, 
                                        const GSList *email_ids,
                                        const gchar *src_folder_id,
                                        const gchar *dest_folder_id,
                                        GError **error);


/*
Assumptions:
when a directory is supplied for putting files into, the directory already exists
no requirement for a method to sync ALL email folders at once.
*/


/*
Outstanding issues:
How do drafts work (AS docs say email can't be added to server using sync command with add)?
How does AS expose 'answered' / 'forwarded'? investigate email2:ConversationIndex (timestamps are added when email replied to/forwarded but not clear how to distinguish between those two things?
*/

/* 
API Questions / todos:
Should MIME files (with email bodies) be unicode?

// TODO define an 'email config' API (for, eg, window_size, filtertype etc rather than passing those options over DBus with each sync...)
*/


G_END_DECLS

#endif
