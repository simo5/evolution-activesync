
#include <glib.h>
#include <gconf/gconf-client.h>
#include <string.h> /* strcmp */
#include "../src/eas-account-list.h"
#include "../src/eas-account.h"

void
test_backup_gconf(GConfClient* client, EasAccountList *account_list, GSList* bkp_list)
{
	EIterator *iter = NULL;
	EasAccount *account = NULL;
	g_print("TEST: test_backup_gconf \n");
	for (iter = e_list_get_iterator (E_LIST ( account_list) );
	     e_iterator_is_valid (iter);
	     e_iterator_next (iter)) {
		account = EAS_ACCOUNT (e_iterator_get (iter));
		bkp_list = g_slist_append (bkp_list, account);
	}
	g_object_unref (iter);
}

void
test_empty_gconf(GConfClient* client)
{
	g_print("TEST: test_empty_gconf \n");	
	gconf_client_recursive_unset(client,
								EAS_ACCOUNT_ROOT,
								0, 					/*GConfUnsetFlags flags,*/
								NULL);
}

void
test_empty_list(GConfClient* client, EasAccountList *account_list)
{
	EIterator *iter = NULL;
	EasAccount *account = NULL;
	g_print("TEST: test_empty_list \n");
	for (iter = e_list_get_iterator (E_LIST ( account_list) );
	     e_iterator_is_valid (iter);
	     e_iterator_next (iter)) {
		account = EAS_ACCOUNT (e_iterator_get (iter));
		eas_account_list_remove	(account_list, account);
	}

	g_object_unref (iter);
}

/* test accounts list changes*/
void
test_scenario_01()
{
	g_print("TEST: test_scenario_01 \n");

}

/* test individual accounts changes*/
void
test_scenario_02()
{
	g_print("TEST: test_scenario_02 \n");
}

/* test account items changes*/
void
test_scenario_03()
{
	g_print("TEST: test_scenario_03 \n");
}

/*
 	test notifications from the GConf
	accounts are added/changed/deleted using GConf APIs only
	data ---> GConf repository ---> EasAccountList
 */
void
test_scenario_04(GConfClient* client)
{
	g_print("Scenario 04 : Notifications from GConf \n");
#if 0
	//-delete existing accounts in GConf
	test_empty_gconf(client);

	int i = 0;
	gchar* uid1 = "email";
	gchar* uid2 = "@cstylianou.com";
	
	gchar* usr_name= "username";
	gchar* server_uri = "https://cstylianou.com/Microsoft-Server-ActiveSync";
	gchar* policy_key ="policy_key";
	
	for (i; i<10; i++){
		/*TODO:
		concatenation:
		 uid: uid = uid1 + i + uid2
		 username = usr_name + i;
		 server_uri = server_uri +i
		 policy_key = policy_key +i
		*/
		
	
		string_Key_len = strlen(EAS_ACCOUNT_ROOT) + 1 + uid_len +
								strlen(EAS_ACCOUNT_KEY_SERVERURI) + 1;
		gchar serveruri_Key_path[string_Key_len];
		g_snprintf(serveruri_Key_path, string_Key_len, "%s/%s%s",
					EAS_ACCOUNT_ROOT, uid, EAS_ACCOUNT_KEY_SERVERURI);


		gconf_client_set_string (account_list->priv->gconf,
								serveruri_Key_path,
								eas_account_get_uri(account),
								NULL);


	//
		string_Key_len = strlen(EAS_ACCOUNT_ROOT) + 1 + uid_len +
								strlen(EAS_ACCOUNT_KEY_USERNAME) + 1;
		gchar username_Key_path[string_Key_len];
		g_snprintf(username_Key_path, string_Key_len, "%s/%s%s",
					EAS_ACCOUNT_ROOT, uid, EAS_ACCOUNT_KEY_USERNAME);

		gconf_client_set_string (account_list->priv->gconf,
						username_Key_path,
						eas_account_get_username(account),
						NULL);

	//
		string_Key_len = strlen(EAS_ACCOUNT_ROOT) + 1 + uid_len +
								strlen(EAS_ACCOUNT_KEY_POLICY_KEY)+ 1;
		gchar policy_key_Key_path[string_Key_len];
		g_snprintf(policy_key_Key_path, string_Key_len, "%s/%s%s",
					EAS_ACCOUNT_ROOT, uid, EAS_ACCOUNT_KEY_POLICY_KEY);

		gconf_client_set_string (account_list->priv->gconf,
								policy_key_Key_path,
								eas_account_get_policy_key(account),
									NULL);
	}




	-Loop and change the accont items
#endif	

}

void
test_restore_gconf(GConfClient* client, EasAccountList *account_list, GSList* bkp_list)
{
	g_print("TEST: test_restore_gconf \n");


	while (bkp_list) {
	gpointer data = bkp_list->data;
	bkp_list = g_slist_remove (bkp_list, bkp_list->data);
	g_object_unref (data);
}
}

gboolean
test_find_account(GConfClient* client, EasAccountList *account_list, const gchar* accountId)
{
/*TODO:
 shouldn't we just use the account_list exposed api: eas_account_list_find()

*/	
	gboolean account_found =FALSE;
	EIterator *iter = NULL;
	EasAccount *account = NULL;
	g_print("Test: test_find_account()\n");
	
	for (iter = e_list_get_iterator (E_LIST ( account_list) ); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		account = EAS_ACCOUNT (e_iterator_get (iter));
		
		if (strcmp (eas_account_get_uid(account), accountId)   == 0) {
			g_print("Found account: \n");
			g_print("account->uid=%s\n", eas_account_get_uid(account));
			g_print("account->uri=%s\n", eas_account_get_uri(account));
			g_print("account->username=%s\n", eas_account_get_username(account));
			g_print("account->policy_key=%s\n", eas_account_get_policy_key(account));
			account_found = TRUE;
			break;
		}
	}

	
	g_object_unref (iter);
	return account_found;
}

void
print_all_accounts(GConfClient* client, EasAccountList *account_list)
{
	EIterator *iter = NULL;
	EasAccount *account = NULL;
	gint  num_of_accounts = 0;
	gint i=0;
	g_print("Test: print_all_accounts()\n");
	g_assert(client != NULL);
	g_assert(account_list != NULL);
	num_of_accounts = e_list_length((EList*)account_list);
		
	g_print(" There are %d accounts in GConf \n", num_of_accounts );
	
	if (!num_of_accounts)
		return;

	for (iter = e_list_get_iterator (E_LIST ( account_list) ); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		account = EAS_ACCOUNT (e_iterator_get (iter));

		g_print("Account %d Info: \n", ++i);
		g_print("account->uid=%s\n", eas_account_get_uid(account));
		g_print("account->uri=%s\n", eas_account_get_uri(account));
		g_print("account->username=%s\n", eas_account_get_username(account));
		g_print("account->policy_key=%s\n", eas_account_get_policy_key(account));
	}

	g_object_unref (iter);
}

void
add_account(GConfClient* client, EasAccountList *account_list, EasAccount* account)
{
	g_print("Test: add_account()\n");
	eas_account_list_add (account_list, account);
	eas_account_list_save_list (account_list);
}

void
delete_account(GConfClient* client, EasAccountList *account_list, EasAccount *account)
{
	g_print("Test: delete_account()\n");
	eas_account_list_remove (account_list, account);
	eas_account_list_save_list (account_list); //TODO: save only the changes
}

void
change_account(GConfClient* client, EasAccountList *account_list, EasAccount *account)
{
	g_print("Test: change_account()\n");
	eas_account_list_change (account_list, account);
//	eas_account_list_save (account_list);//TODO: save only the changes
//	eas_account_list_save_account (account_list);//TODO: save only the changes
}

void
delete_all_accounts(GConfClient* client, EasAccountList *account_list)
{
	g_print("Test: delete_all_accounts()\n");
//TODO: loop through the list
	//eas_account_list_remove (account_list, account)
//Then save
	//eas_account_list_save (account_list);

}

void
add_list_of_accounts(GConfClient* client, EasAccountList *account_list)
{
		g_print("Test: add_list_of_accounts()\n");
//TODO:
}

int main (int argc, char** argv) {
	GMainLoop* mainloop = NULL;
	GConfClient* client = NULL;
	EasAccountList *account_list = NULL;
	EIterator *iter = NULL;
	GSList* bkp_list = NULL;
		
		
	g_print("main Starting.\n");
	g_type_init();

	mainloop = g_main_loop_new(NULL, FALSE);
	if (mainloop == NULL) {
		g_error(" Failed to create mainloop!\n");
	}

	
	client = gconf_client_get_default();
	g_assert(client != NULL);
	/* Get list of accounts from gconf repository */
	account_list = eas_account_list_new (client);
	g_assert(account_list != NULL);	

	test_backup_gconf(client, account_list, bkp_list);
	test_empty_gconf(client);
	/* Add test scenarios here */
	test_scenario_01();
	test_scenario_02();
	test_scenario_03();
	/* End of test scenarios */
	test_restore_gconf(client, account_list, bkp_list);

		
#if 0		
	g_print( "Test01: Add new account without policy key\n");	
	EasAccount* account00= NULL;
	account00 = eas_account_new();
	eas_account_set_uid	(account00, "new.account00@mobica.com");
	eas_account_set_uri	(account00, "https://cstylianou.com/Microsoft-Server-ActiveSync");
	eas_account_set_username(account00, "new.account00");
	
	eas_account_list_add (account_list, account00);
	eas_account_list_save_list (account_list);

	eas_account_set_policy_key(account00, "new.account00 policy_key 1");
	eas_account_list_save_list (account_list);
		                    
	eas_account_set_policy_key(account00, "new.account00 policy_key 2");
	eas_account_list_save_list (account_list);

	eas_account_set_policy_key(account00, "new.account00 policy_key 3");
	eas_account_list_save_list (account_list);



	//test05: add one account
	g_print( "Test01: Add new account\n");	
	EasAccount* account01= NULL;
	account01 = eas_account_new();
	eas_account_set_uid	(account01, "new.account01@mobica.com");
	eas_account_set_uri	(account01, "https://cstylianou.com/Microsoft-Server-ActiveSync");
	eas_account_set_username(account01, "new.account01");
	eas_account_set_policy_key(account01, "new.account01 policy_key");

	g_print( " test uid=%s\n", 	eas_account_get_uid	(account01));
	g_print( " test uri=%s\n", 	eas_account_get_uri	(account01));
	g_print( " test username=%s\n", 	eas_account_get_username(account01));
	g_print( " test password=%s\n", 	eas_account_get_password(account01));
	g_print( " test policy_key=%s\n", 	eas_account_get_policy_key(account01));

	eas_account_list_save_account(account_list, account01);
	//print_all_accounts(client, account_list);
#endif
		                    
#if 0
	/* save policy_key*/
	g_print( "Test02: Set Policy key for an eccount\n");
	EasAccount* account02= NULL;
	account02 = eas_account_new();
	eas_account_set_uid	(account02, "brahim@mobica.com");
	//g_print( " test uid=%s\n", 	eas_account_get_uid	(account01));
	//g_print( " test uri=%s\n", 	eas_account_get_uri	(account01));
	//g_print( " test username=%s\n", 	eas_account_get_username(account01));
	//g_print( " test password=%s\n", 	eas_account_get_password(account01));	
	eas_account_set_policy_key	(account02, "brahim policy_key 01");
	eas_account_list_save_item(account_list,
								account02,
								EAS_ACCOUNT_POLICY_KEY);

	//print_all_accounts(client, account_list);

	/* save policy_key*/
	g_print( "Test03: Set Policy key for an eccount 2\n");
	EasAccount* account03= NULL;
	account03 = eas_account_new();
	eas_account_set_uid	(account03, "brahim@mobica.com");
	//g_print( " test uid=%s\n", 	eas_account_get_uid	(account01));
	//g_print( " test uri=%s\n", 	eas_account_get_uri	(account01));
	//g_print( " test username=%s\n", 	eas_account_get_username(account01));
	//g_print( " test password=%s\n", 	eas_account_get_password(account01));	
	eas_account_set_policy_key	(account03, "brahim policy_key 02");
	eas_account_list_save_item(account_list,
								account03,
								EAS_ACCOUNT_POLICY_KEY);

	//print_all_accounts(client, account_list);

	test_find_account(client, account_list, "brahim@mobica.com");

#endif



#if 0	
	//test03: search for an account (pre-condition: GConf is populated with few accounts)
	const gchar* accountId ="brahim@mobica.com";
	if (!test_find_account(client, account_list, accountId)){
		g_print( " account not found in GConf\n");
	}
		



	//test03: search for an account (pre-condition: GConf is populated with few accounts)
	const gchar* accountId ="brahim@mobica.com";
	if (!test_find_account(client, account_list, accountId)){
		g_print( " account not found in GConf\n");
	}

	//test04: search for an account (precondition: account doesn't exists)
	accountId ="xxxx@xxxx.xxx";
	if (!test_find_account(client, account_list, accountId)){
		g_print( " account not found in GConf\n");
	}

	print_all_accounts(client, account_list);

	//test05: add one account
	EasAccount* account01= NULL;
	account01 = eas_account_new();
	eas_account_set_uid	(account01, "brahim@mobica.com");
	eas_account_set_uri	(account01, "https://cstylianou.com/Microsoft-Server-ActiveSync");
	eas_account_set_username(account01, "brahim");
	eas_account_set_password(account01, "M0bica!");

	g_print( " test uid=%s\n", 	eas_account_get_uid	(account01));
	g_print( " test uri=%s\n", 	eas_account_get_uri	(account01));
	g_print( " test username=%s\n", 	eas_account_get_username(account01));
	g_print( " test password=%s\n", 	eas_account_get_password(account01));

	
	//populate EasAccount	
	add_account(client, account_list, account01);
	//print_all_accounts(client, account_list);	
	print_all_accounts(client, account_list);

	//test01: empty GConf accounts
	delete_all_accounts(client, account_list);
	//print_all_accounts(client, account_list);

	
	
	//test02: add a list of accounts
	add_list_of_accounts(client, account_list);
	//print_all_accounts(client, account_list);
	
	//test03: search for an account (pre-condition: GConf is populated with few accounts)
	const gchar* accountId ="brahim@mobica.com";
	if (!test_find_account(client, account_list, accountId)){
		g_print( " account not found in GConf\n");
	}

	//test04: search for an account (precondition: account doesn't exists)
	accountId ="xxxx@xxxx.xxx";
	if (!test_find_account(client, account_list, accountId)){
		g_print( " account not found in GConf\n");
	}

	//test05: add one account
	EasAccount* account= NULL;
	//TODO: populate EasAccount	
	add_account(client, account_list, account);
	//print_all_accounts(client, account_list);
	
	//test06: change an existing account
	//accountId ="brahim@mobica.com";
	EasAccount *acc_to_change = NULL;
	change_account(client, account_list, acc_to_change);
	//print_all_accounts(client, account_list);
	
	//test07: delete an existing account
	EasAccount *acc_to_delete = NULL;	
	delete_account(client, account_list, acc_to_delete);
	//print_all_accounts(client, account_list);
#endif
	g_main_loop_run(mainloop);

	g_print( "main Out of main loop (shouldn't happen)\n");



	g_object_unref (iter);
	g_object_unref (account_list);
	g_object_unref(client);	
		
	/* Release the mainloop object. */
	g_main_loop_unref(mainloop);

	g_print( "main Ending\n");

	return 0;
	}

