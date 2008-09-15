/* $Id$ */

/*
 *  Copyright (c) 2003-2007 Axel Andersson
 *  All rights reserved_
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1_ Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer_
 *  2_ Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution_
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED_  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE_
 */

#ifndef WD_ACCOUNTS_H
#define WD_ACCOUNTS_H 1

#include <wired/wired.h>
#include "main.h"

struct _wd_account {
	wi_runtime_base_t				base;
	
	wi_string_t						*name;
	wi_string_t						*full_name;
	wi_date_t						*creation_time;
	wi_date_t						*modification_time;
	wi_date_t						*login_time;
	wi_string_t						*edited_by;
	wi_string_t						*password;
	wi_string_t						*group;
	wi_array_t						*groups;
	wi_string_t						*files;

	wi_boolean_t					user_cannot_set_nick;
	wi_boolean_t					user_get_info;
	wi_boolean_t					user_kick_users;
	wi_boolean_t					user_ban_users;
	wi_boolean_t					user_cannot_be_disconnected;
	wi_boolean_t					chat_set_topic;
	wi_boolean_t					chat_create_chats;
	wi_boolean_t					message_send_messages;
	wi_boolean_t					message_broadcast;
	wi_boolean_t					news_read_news;
	wi_boolean_t					news_post_news;
	wi_boolean_t					news_clear_news;
	wi_boolean_t					file_list_files;
	wi_boolean_t					file_get_info;
	wi_boolean_t					file_create_directories;
	wi_boolean_t					file_create_links;
	wi_boolean_t					file_move_files;
	wi_boolean_t					file_rename_files;
	wi_boolean_t					file_set_type;
	wi_boolean_t					file_set_comment;
	wi_boolean_t					file_set_permissions;
	wi_boolean_t					file_set_executable;
	wi_boolean_t					file_delete_files;
	wi_boolean_t					file_access_all_dropboxes;
	wi_p7_uint32_t					file_recursive_list_depth_limit;
	wi_boolean_t					transfer_download_files;
	wi_boolean_t					transfer_upload_files;
	wi_boolean_t					transfer_upload_directories;
	wi_boolean_t					transfer_upload_anywhere;
	wi_p7_uint32_t					transfer_download_limit;
	wi_p7_uint32_t					transfer_upload_limit;
	wi_p7_uint32_t					transfer_download_speed_limit;
	wi_p7_uint32_t					transfer_upload_speed_limit;
	wi_boolean_t					account_change_password;
	wi_boolean_t					account_list_accounts;
	wi_boolean_t					account_read_accounts;
	wi_boolean_t					account_create_accounts;
	wi_boolean_t					account_edit_accounts;
	wi_boolean_t					account_delete_accounts;
	wi_boolean_t					account_raise_account_privileges;
	wi_boolean_t					user_get_users;
	wi_boolean_t					log_view_log;
	wi_boolean_t					settings_get_settings;
	wi_boolean_t					settings_set_settings;
	wi_boolean_t					tracker_list_servers;
	wi_boolean_t					tracker_register_servers;
};
typedef struct _wd_account			wd_account_t;


void								wd_accounts_init(void);

wd_account_t *						wd_accounts_read_user_and_group(wi_string_t *);
wd_account_t *						wd_accounts_read_user(wi_string_t *);
wd_account_t *						wd_accounts_read_group(wi_string_t *);
wi_string_t *						wd_accounts_password_for_user(wi_string_t *);
wi_boolean_t						wd_accounts_change_password(wd_account_t *, wi_string_t *);
wi_boolean_t						wd_accounts_create_user(wd_account_t *);
wi_boolean_t						wd_accounts_create_group(wd_account_t *);
wi_boolean_t						wd_accounts_edit_user(wd_account_t *);
wi_boolean_t						wd_accounts_edit_group(wd_account_t *);
wi_boolean_t						wd_accounts_delete_user(wi_string_t *);
wi_boolean_t						wd_accounts_delete_group(wi_string_t *);
wi_boolean_t						wd_accounts_clear_group(wi_string_t *);
void								wd_accounts_update_login_time(wd_account_t *);
void								wd_accounts_reload_user_account(wi_string_t *);
void								wd_accounts_reload_group_account(wi_string_t *);
void								wd_accounts_reload_all_accounts(void);

void								wd_accounts_reply_user_list(wd_user_t *, wi_p7_message_t *);
void								wd_accounts_reply_group_list(wd_user_t *, wi_p7_message_t *);

wd_account_t *						wd_account_alloc(void);
wd_account_t *						wd_account_init_with_message(wd_account_t *, wi_p7_message_t *);
wd_account_t *						wd_account_init_user_with_array(wd_account_t *, wi_array_t *);
wd_account_t *						wd_account_init_group_with_array(wd_account_t *, wi_array_t *);

wi_p7_message_t *					wd_account_privileges_message(wd_account_t *);
void								wd_account_reply_user_account(wd_account_t *, wd_user_t *, wi_p7_message_t *);
void								wd_account_reply_group_account(wd_account_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t						wd_account_check_privileges(wd_account_t *, wd_user_t *);

#endif /* WD_ACCOUNTS_H */
