/*
 * Filename: eas-mail.c
 */

#include "eas-mail.h"
#include "eas-mail-stub.h"


G_DEFINE_TYPE (EasMail, eas_mail, G_TYPE_OBJECT);

static void
eas_mail_init (EasMail *object)
{
	/* TODO: Add initialization code here */
}

static void
eas_mail_finalize (GObject *object)
{
	/* TODO: Add deinitalization code here */

	G_OBJECT_CLASS (eas_mail_parent_class)->finalize (object);
}

static void
eas_mail_class_init (EasMailClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	GObjectClass* parent_class = G_OBJECT_CLASS (klass);

	object_class->finalize = eas_mail_finalize;
	
	 /* Binding to GLib/D-Bus" */ 
    dbus_g_object_type_install_info(EAS_TYPE_MAIL,
                                            &dbus_glib_eas_mail_object_info);
}



gboolean eas_mail_start_sync(EasMail* easMailObj, gint valueIn, GError** error)
{
/*
guint64 account_uid = 12345;
const gchar* sync_key =NULL; 
gchar **ret_sync_key =NULL;
gchar **ret_created_folders_array =NULL;
gchar **ret_updated_folders_array =NULL;
gchar **ret_deleted_folders_array =NULL;


eas_connection_folder_sync(easMailObj->easConnection, 
                                            account_uid,
						sync_key, 
						ret_sync_key,  
						ret_created_folders_array,
						ret_updated_folders_array,
						ret_deleted_folders_array,
						error);
*/
  return TRUE;
}

void eas_mail_test_001(EasMail* obj, DBusGMethodInvocation* context)
{

 	g_print(">> eas_mail_test_001()\n");
        GError *error = NULL;
        gchar *ok_str = g_strdup ("OK");
         // ...

        if (error) {
		g_print(">> eas_mail_test_001 -error-\n");
                dbus_g_method_return_error (context, error);
                g_error_free (error);
        } else{
		g_print(">> eas_mail_test_001 -No error-\n");
                dbus_g_method_return (context, ok_str);
	}

        g_free (ok_str);
}

gboolean eas_mail_set_eas_connection(EasMail* easMailObj, EasConnection* easConnObj)
{
  gboolean ret= FALSE;

  if(easConnObj != NULL){
  	easMailObj->easConnection = easConnObj;
	ret= TRUE;
  }
  return ret;
}

gboolean eas_mail_sync_email_folder_hierarchy(EasMail* easMailObj, 
                                            guint64 account_uid,
											const gchar* sync_key, 
											gchar **ret_sync_key,  
											gchar **ret_created_folders_array,
											gchar **ret_updated_folders_array,
											gchar **ret_deleted_folders_array,
											GError** error)
{

//guint64 account_uid = 12345;
//const gchar* sync_key =NULL; 
//gchar **ret_sync_key =NULL;
//gchar **ret_created_folders_array =NULL;
//gchar **ret_updated_folders_array =NULL;
//gchar **ret_deleted_folders_array =NULL;
#if 0
	g_print("eas_mail_sync_email_folder_hierarchy++");
	
	eas_connection_folder_sync(easMailObj->easConnection, 
		                    account_uid,
							sync_key, 
							ret_sync_key,  
							ret_created_folders_array,
							ret_updated_folders_array,
							ret_deleted_folders_array,
							error);
#endif

	g_print("eas_mail_sync_email_folder_hierarchy--");
	// TODO ?

  return TRUE;
}


gboolean eas_mail_sync_folder_email(EasMail* easMailObj, 
                                    guint64 account_uid,
									const gchar* sync_key,   
                                    gboolean get_server_changes,
									const gchar *collection_id,	//folder to sync
									const gchar* deleted_email_array,
									const gchar* changed_email_array,                                    
									gchar *ret_sync_key,                                    
									gboolean *ret_more_available,
									gchar **ret_added_email_array,
									gchar **ret_deleted_email_array,
									gchar **ret_changed_email_array,	
									GError** error)
{
	// TODO
  return TRUE;											
}


gboolean
eas_mail_fetch (EasMail* easMailObj, 
                guint64 account_uid, 
                const gchar *server_id, 
                const gchar *collection_id, 
                const gchar *file_reference, 
                const gchar *mime_directory, 
                GError **error)
{
	// TODO
	return TRUE;
}

// TODO - finalise this API
gboolean eas_mail_send_email(EasMail* easMailObj, 
								guint64 account_uid,                             
								const gchar* clientid,
								const gchar* accountid,
								gboolean save_in_sent_items,
								const gchar *mime,
								GError** error)
{
  return TRUE;								
}


