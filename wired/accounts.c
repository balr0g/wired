/* $Id$ */

/*
 *  Copyright (c) 2003-2007 Axel Andersson
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <stddef.h>
#include <string.h>
#include <wired/wired.h>

#include "accounts.h"
#include "chats.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "users.h"

#define WD_ACCOUNT_FIELD_TYPE						"type"
#define WD_ACCOUNT_FIELD_ACCOUNT					"account"
#define WD_ACCOUNT_FIELD_REQUIRED					"required"


struct _wd_account {
	wi_runtime_base_t								base;
	
	wi_dictionary_t									*values;
};

enum _wd_account_field_type {
	WD_ACCOUNT_FIELD_STRING							= 0,
	WD_ACCOUNT_FIELD_DATE,
	WD_ACCOUNT_FIELD_NUMBER,
	WD_ACCOUNT_FIELD_BOOLEAN,
	WD_ACCOUNT_FIELD_LIST
};
typedef enum _wd_account_field_type					wd_account_field_type_t;

enum _wd_account_field_account {
	WD_ACCOUNT_FIELD_USER_LIST						= (1 << 0),
	WD_ACCOUNT_FIELD_GROUP_LIST						= (1 << 1),
	WD_ACCOUNT_FIELD_USER							= (1 << 2),
	WD_ACCOUNT_FIELD_GROUP							= (1 << 3),
	WD_ACCOUNT_FIELD_PRIVILEGE						= (1 << 4),
	WD_ACCOUNT_FIELD_USER_AND_GROUP					= (WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_GROUP),
	WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST		= (WD_ACCOUNT_FIELD_USER_LIST | WD_ACCOUNT_FIELD_GROUP_LIST),
	WD_ACCOUNT_FIELD_USER_AND_PRIVILEGE				= (WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_PRIVILEGE),
	WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE	= (WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_GROUP | WD_ACCOUNT_FIELD_PRIVILEGE)
};
typedef enum _wd_account_field_account				wd_account_field_account_t;


static wi_enumerator_t *							wd_accounts_instance_at_path(wi_string_t *);
static wi_array_t *									wd_accounts_convert_accounts(wi_string_t *, wi_array_t *);
static void											wd_accounts_reload_account(wd_user_t *, wd_account_t *);

static wd_account_t *								wd_account_init_with_values(wd_account_t *, wi_dictionary_t *);
static wi_string_t *								wd_account_description(wi_runtime_instance_t *);
static void											wd_account_dealloc(wi_runtime_instance_t *);

static void											wd_account_read_from_message(wd_account_t *, wi_p7_message_t *);
static void											wd_account_write_to_message(wd_account_t *, wi_uinteger_t, wi_p7_message_t *);

static wi_string_t									*wd_users_path;
static wi_string_t									*wd_groups_path;

static wi_recursive_lock_t							*wd_users_lock;
static wi_recursive_lock_t							*wd_groups_lock;

static wi_dictionary_t								*wd_account_fields;
static wi_array_t									*wd_users_fields;
static wi_array_t									*wd_groups_fields;

static wi_runtime_id_t								wd_account_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t							wd_account_runtime_class = {
	"wd_account_t",
	wd_account_dealloc,
	NULL,
	NULL,
	wd_account_description,
	NULL
};


#define WD_ACCOUNT_FIELD_DICTIONARY(type, account, required)						\
	wi_dictionary_with_data_and_keys(												\
		WI_INT32((type)),					WI_STR(WD_ACCOUNT_FIELD_TYPE),			\
		WI_INT32((account)),				WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),		\
		wi_number_with_bool((required)),	WI_STR(WD_ACCOUNT_FIELD_REQUIRED),		\
		NULL)

void wd_accounts_init(void) {
	wd_account_runtime_id = wi_runtime_register_class(&wd_account_runtime_class);

	wd_users_path = WI_STR("users");
	wd_groups_path = WI_STR("groups");
	
	wd_users_lock = wi_recursive_lock_init(wi_recursive_lock_alloc());
	wd_groups_lock = wi_recursive_lock_init(wi_recursive_lock_alloc());
	
	wd_account_fields = wi_dictionary_init_with_data_and_keys(wi_dictionary_alloc(),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_STRING, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST, true),
			WI_STR("wired.account.name"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_STRING, WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST, true),
			WI_STR("wired.account.full_name"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_DATE, WD_ACCOUNT_FIELD_USER_AND_GROUP | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST, true),
			WI_STR("wired.account.creation_time"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_DATE, WD_ACCOUNT_FIELD_USER_AND_GROUP | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST, true),
			WI_STR("wired.account.modification_time"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_DATE, WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST, true),
			WI_STR("wired.account.login_time"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_STRING, WD_ACCOUNT_FIELD_USER_AND_GROUP | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST, true),
			WI_STR("wired.account.edited_by"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_STRING, WD_ACCOUNT_FIELD_USER_AND_PRIVILEGE | WD_ACCOUNT_FIELD_USER_LIST, true),
			WI_STR("wired.account.group"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_LIST, WD_ACCOUNT_FIELD_USER_AND_PRIVILEGE | WD_ACCOUNT_FIELD_USER_LIST, true),
			WI_STR("wired.account.groups"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_STRING, WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST, true),
			WI_STR("wired.account.password"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_STRING, WD_ACCOUNT_FIELD_USER_AND_GROUP, false),
			WI_STR("wired.account.files"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.user.get_info"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.user.kick_users"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.user.ban_users"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.user.cannot_be_disconnected"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.user.cannot_set_nick"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.user.get_users"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.chat.set_topic"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.chat.create_chats"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.message.send_messages"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.message.broadcast"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.list_files"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.get_info"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.create_links"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.rename_files"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.set_type"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.set_comment"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.set_permissions"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.set_executable"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_NUMBER, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.recursive_list_depth_limit"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.create_directories"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.move_files"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.delete_files"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.file.access_all_dropboxes"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.change_password"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.list_accounts"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.read_accounts"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.create_users"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.edit_users"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.delete_users"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.create_groups"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.edit_groups"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.delete_groups"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.account.raise_account_privileges"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.transfer.download_files"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.transfer.upload_files"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.transfer.upload_anywhere"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.transfer.upload_directories"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_NUMBER, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.transfer.download_speed_limit"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_NUMBER, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.transfer.upload_speed_limit"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_NUMBER, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.transfer.download_limit"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_NUMBER, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.transfer.upload_limit"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.read_boards"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.log.view_log"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.settings.get_settings"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.settings.set_settings"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.banlist.get_bans"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.banlist.add_bans"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.banlist.delete_bans"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.add_boards"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.move_boards"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.rename_boards"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.delete_boards"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.set_permissions"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.add_threads"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.move_threads"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.delete_threads"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.add_posts"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.edit_own_posts"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.edit_all_posts"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.board.delete_posts"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.tracker.list_servers"),
		WD_ACCOUNT_FIELD_DICTIONARY(WD_ACCOUNT_FIELD_BOOLEAN, WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE, false),
			WI_STR("wired.account.tracker.register_servers"),
		NULL);
	
	wd_users_fields = wi_array_init_with_data(wi_array_alloc(),
		WI_STR("wired.account.name"),
		WI_STR("wired.account.password"),
		WI_STR("wired.account.group"),	
		WI_STR("wired.account.user.get_info"),
		WI_STR("wired.account.message.broadcast"),
		WI_STR("wired.account.board.add_posts"),
		WI_STR("wired.account.board.delete_posts"),
		WI_STR("wired.account.transfer.download_files"),
		WI_STR("wired.account.transfer.upload_files"),
		WI_STR("wired.account.transfer.upload_anywhere"),
		WI_STR("wired.account.file.create_directories"),
		WI_STR("wired.account.file.move_files"),
		WI_STR("wired.account.file.delete_files"),
		WI_STR("wired.account.file.access_all_dropboxes"),
		WI_STR("wired.account.account.create_users"),
		WI_STR("wired.account.account.edit_users"),
		WI_STR("wired.account.account.delete_users"),
		WI_STR("wired.account.account.raise_account_privileges"),
		WI_STR("wired.account.user.kick_users"),
		WI_STR("wired.account.user.ban_users"),	
		WI_STR("wired.account.user.cannot_be_disconnected"),
		WI_STR("wired.account.transfer.download_speed_limit"),
		WI_STR("wired.account.transfer.upload_speed_limit"),
		WI_STR("wired.account.transfer.download_limit"),
		WI_STR("wired.account.transfer.upload_limit"),
		WI_STR("wired.account.chat.set_topic"),
		WI_STR("wired.account.files"),
		WI_STR("wired.account.full_name"),
		WI_STR("wired.account.creation_time"),
		WI_STR("wired.account.modification_time"),
		WI_STR("wired.account.login_time"),
		WI_STR("wired.account.edited_by"),
		WI_STR("wired.account.groups"),	
		WI_STR("wired.account.chat.create_chats"),
		WI_STR("wired.account.message.send_messages"),
		WI_STR("wired.account.board.read_boards"),
		WI_STR("wired.account.file.list_files"),
		WI_STR("wired.account.file.get_info"),
		WI_STR("wired.account.file.create_links"),
		WI_STR("wired.account.file.rename_files"),
		WI_STR("wired.account.file.set_type"),
		WI_STR("wired.account.file.set_comment"),
		WI_STR("wired.account.file.set_permissions"),
		WI_STR("wired.account.file.set_executable"),
		WI_STR("wired.account.file.recursive_list_depth_limit"),
		WI_STR("wired.account.transfer.upload_directories"),
		WI_STR("wired.account.account.change_password"),
		WI_STR("wired.account.account.list_accounts"),
		WI_STR("wired.account.account.read_accounts"),
		WI_STR("wired.account.user.cannot_set_nick"),
		WI_STR("wired.account.user.get_users"),
		WI_STR("wired.account.log.view_log"),
		WI_STR("wired.account.settings.get_settings"),
		WI_STR("wired.account.settings.set_settings"),
		WI_STR("wired.account.banlist.get_bans"),
		WI_STR("wired.account.banlist.add_bans"),
		WI_STR("wired.account.banlist.delete_bans"),
		WI_STR("wired.account.tracker.list_servers"),
		WI_STR("wired.account.tracker.register_servers"),
		WI_STR("wired.account.board.add_boards"),
		WI_STR("wired.account.board.move_boards"),
		WI_STR("wired.account.board.rename_boards"),
		WI_STR("wired.account.board.delete_boards"),
		WI_STR("wired.account.board.set_permissions"),
		WI_STR("wired.account.board.add_threads"),
		WI_STR("wired.account.board.move_threads"),
		WI_STR("wired.account.board.delete_threads"),
		WI_STR("wired.account.board.edit_own_posts"),
		WI_STR("wired.account.board.edit_all_posts"),
		NULL);
	
	wd_groups_fields = wi_array_init_with_data(wi_array_alloc(),
		WI_STR("wired.account.name"),
		WI_STR("wired.account.user.get_info"),
		WI_STR("wired.account.message.broadcast"),
		WI_STR("wired.account.board.add_posts"),
		WI_STR("wired.account.board.delete_posts"),
		WI_STR("wired.account.transfer.download_files"),
		WI_STR("wired.account.transfer.upload_files"),
		WI_STR("wired.account.transfer.upload_anywhere"),
		WI_STR("wired.account.file.create_directories"),
		WI_STR("wired.account.file.move_files"),
		WI_STR("wired.account.file.delete_files"),
		WI_STR("wired.account.file.access_all_dropboxes"),
		WI_STR("wired.account.account.create_users"),
		WI_STR("wired.account.account.edit_users"),
		WI_STR("wired.account.account.delete_users"),
		WI_STR("wired.account.account.raise_account_privileges"),
		WI_STR("wired.account.user.kick_users"),
		WI_STR("wired.account.user.ban_users"),	
		WI_STR("wired.account.user.cannot_be_disconnected"),
		WI_STR("wired.account.transfer.download_speed_limit"),
		WI_STR("wired.account.transfer.upload_speed_limit"),
		WI_STR("wired.account.transfer.download_limit"),
		WI_STR("wired.account.transfer.upload_limit"),
		WI_STR("wired.account.chat.set_topic"),
		WI_STR("wired.account.files"),
		WI_STR("wired.account.creation_time"),
		WI_STR("wired.account.modification_time"),
		WI_STR("wired.account.edited_by"),
		WI_STR("wired.account.chat.create_chats"),
		WI_STR("wired.account.message.send_messages"),
		WI_STR("wired.account.board.read_boards"),
		WI_STR("wired.account.file.list_files"),
		WI_STR("wired.account.file.get_info"),
		WI_STR("wired.account.file.create_links"),
		WI_STR("wired.account.file.rename_files"),
		WI_STR("wired.account.file.set_type"),
		WI_STR("wired.account.file.set_comment"),
		WI_STR("wired.account.file.set_permissions"),
		WI_STR("wired.account.file.set_executable"),
		WI_STR("wired.account.file.recursive_list_depth_limit"),
		WI_STR("wired.account.transfer.upload_directories"),
		WI_STR("wired.account.account.change_password"),
		WI_STR("wired.account.account.list_accounts"),
		WI_STR("wired.account.account.read_accounts"),
		WI_STR("wired.account.user.cannot_set_nick"),
		WI_STR("wired.account.user.get_users"),
		WI_STR("wired.account.log.view_log"),
		WI_STR("wired.account.settings.get_settings"),
		WI_STR("wired.account.settings.set_settings"),
		WI_STR("wired.account.banlist.get_bans"),
		WI_STR("wired.account.banlist.add_bans"),
		WI_STR("wired.account.banlist.delete_bans"),
		WI_STR("wired.account.tracker.list_servers"),
		WI_STR("wired.account.tracker.register_servers"),
		WI_STR("wired.account.board.add_boards"),
		WI_STR("wired.account.board.move_boards"),
		WI_STR("wired.account.board.rename_boards"),
		WI_STR("wired.account.board.delete_boards"),
		WI_STR("wired.account.board.set_permissions"),
		WI_STR("wired.account.board.add_threads"),
		WI_STR("wired.account.board.move_threads"),
		WI_STR("wired.account.board.delete_threads"),
		WI_STR("wired.account.board.edit_own_posts"),
		WI_STR("wired.account.board.edit_all_posts"),
		NULL);
}



#pragma mark -

wd_account_t * wd_accounts_read_user_and_group(wi_string_t *name) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_string_t				*field_name, *group_name;
	wi_runtime_instance_t	*value;
	wd_account_t			*user, *group;
	
	user = wd_accounts_read_user(name);
	
	if(!user)
		return NULL;
	
	group_name = wd_account_group(user);
	
	if(group_name && wi_string_length(group_name) > 0) {
		group = wd_accounts_read_group(group_name);
		
		if(group) {
			enumerator = wi_dictionary_key_enumerator(wd_account_fields);
			
			while((field_name = wi_enumerator_next_data(enumerator))) {
				field = wi_dictionary_data_for_key(wd_account_fields, field_name);
				
				if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & WD_ACCOUNT_FIELD_PRIVILEGE ||
				   wi_is_equal(field_name, WI_STR("wired.account.files"))) {
					value = wi_dictionary_data_for_key(user->values, field_name);
					
					if(!value) {
						value = wi_dictionary_data_for_key(group->values, field_name);
						
						if(value)
							wi_dictionary_set_data_for_key(user->values, value, field_name);
					}
				}
			}
		}
	}
	
	return user;
}



wd_account_t * wd_accounts_read_user(wi_string_t *name) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_runtime_instance_t	*instance;
	wd_account_t			*account = NULL;
	
	wi_recursive_lock_lock(wd_users_lock);
	
	instance = wi_plist_read_instance_from_file(wd_users_path);
	
	if(!instance) {
		instance = wd_accounts_convert_accounts(wd_users_path, wd_users_fields);
		
		if(instance)
			wi_log_info(WI_STR("Converted users to \"%@\""), wd_users_path);
	}
	
	if(instance) {
		if(wi_runtime_id(instance) == wi_array_runtime_id()) {
			enumerator = wi_array_data_enumerator(instance);
			
			while((dictionary = wi_enumerator_next_data(enumerator))) {
				if(wi_runtime_id(dictionary) == wi_dictionary_runtime_id()) {
					if(wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.name")), name)) {
						account = wd_account_init_with_values(wd_account_alloc(), dictionary);
						
						break;
					}
				}
			}
		} else {
			wi_log_err(WI_STR("Could not read %@: Not an array"), wd_users_path);
		}
	} else {
		wi_log_err(WI_STR("Could not read %@: %m"), wd_users_path);
	}
	
	wi_recursive_lock_unlock(wd_users_lock);
	
	return wi_autorelease(account);
}



wd_account_t * wd_accounts_read_group(wi_string_t *name) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_runtime_instance_t	*instance;
	wd_account_t			*account = NULL;
	
	wi_recursive_lock_lock(wd_groups_lock);
	
	instance = wi_plist_read_instance_from_file(wd_groups_path);
	
	if(!instance) {
		instance = wd_accounts_convert_accounts(wd_groups_path, wd_groups_fields);
		
		if(instance)
			wi_log_info(WI_STR("Converted groups to \"%@\""), wd_groups_path);
	}
	
	if(instance) {
		if(wi_runtime_id(instance) == wi_array_runtime_id()) {
			enumerator = wi_array_data_enumerator(instance);
			
			while((dictionary = wi_enumerator_next_data(enumerator))) {
				if(wi_runtime_id(dictionary) == wi_dictionary_runtime_id()) {
					if(wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.name")), name)) {
						account = wd_account_init_with_values(wd_account_alloc(), dictionary);
						
						break;
					}
				}
			}
		} else {
			wi_log_err(WI_STR("Could not read %@: Not an array"), wd_groups_path);
		}
	} else {
		wi_log_err(WI_STR("Could not read %@: %m"), wd_groups_path);
	}
	
	wi_recursive_lock_unlock(wd_groups_lock);
	
	return wi_autorelease(account);
}



wi_string_t * wd_accounts_password_for_user(wi_string_t *name) {
	wd_account_t	*account;
	
	if(wi_string_length(name) == 0)
		name = WI_STR("guest");
	
	account = wd_accounts_read_user(name);
		
	if(account)
		return wd_account_password(account);
	
	return NULL;
}



wi_boolean_t wd_accounts_change_password(wd_account_t *account, wi_string_t *password) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_array_t				*array;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			result = false;
	
	wi_dictionary_set_data_for_key(account->values, password, WI_STR("wired.account.password"));
	
	wi_recursive_lock_lock(wd_users_lock);
	
	instance	= wd_accounts_instance_at_path(wd_users_path);
	array		= wi_array();
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		if(!wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.name")), wd_account_name(account)))
			wi_array_add_data(array, dictionary);
	}

	wi_array_add_data(array, account->values);
	
	result = wi_plist_write_instance_to_file(array, wd_users_path);
	
	if(!result)
		wi_log_err(WI_STR("Could not write %@: %m"));
	
	wi_recursive_lock_unlock(wd_users_lock);

	return result;
}



wi_boolean_t wd_accounts_create_user(wd_account_t *account, wd_user_t *user) {
	wi_runtime_instance_t		*instance;
	wi_boolean_t				result;
	
	wi_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.creation_time"));
	wi_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	wi_recursive_lock_lock(wd_users_lock);

	instance = wd_accounts_instance_at_path(wd_users_path);
	
	wi_array_add_data(instance, account->values);
	
	result = wi_plist_write_instance_to_file(instance, wd_users_path);
	
	if(!result)
		wi_log_err(WI_STR("Could not write %@: %m"));

	wi_recursive_lock_unlock(wd_users_lock);
	
	return result;
}



wi_boolean_t wd_accounts_create_group(wd_account_t *account, wd_user_t *user) {
	wi_runtime_instance_t		*instance;
	wi_boolean_t				result;
	
	wi_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.creation_time"));
	wi_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	wi_recursive_lock_lock(wd_groups_lock);

	instance = wd_accounts_instance_at_path(wd_groups_path);
	
	wi_array_add_data(instance, account->values);
	
	result = wi_plist_write_instance_to_file(instance, wd_groups_path);
	
	if(!result)
		wi_log_err(WI_STR("Could not write %@: %m"));

	wi_recursive_lock_unlock(wd_groups_lock);
	
	return result;
}



wi_boolean_t wd_accounts_edit_user(wd_account_t *account, wd_user_t *user) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_array_t				*array;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			result = false;
	
	wi_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.modification_time"));
	wi_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	wi_recursive_lock_lock(wd_users_lock);
	
	instance	= wd_accounts_instance_at_path(wd_users_path);
	array		= wi_array();
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		if(!wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.name")), wd_account_name(account)))
			wi_array_add_data(array, dictionary);
	}

	wi_array_add_data(array, account->values);
	
	result = wi_plist_write_instance_to_file(array, wd_users_path);
		
	if(!result)
		wi_log_err(WI_STR("Could not write %@: %m"));

	wi_recursive_lock_unlock(wd_users_lock);

	return result;
}



wi_boolean_t wd_accounts_edit_group(wd_account_t *account, wd_user_t *user) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_array_t				*array;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			result = false;
	
	wi_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.modification_time"));
	wi_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	wi_recursive_lock_lock(wd_groups_lock);
	
	instance	= wd_accounts_instance_at_path(wd_groups_path);
	array		= wi_array();
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		if(!wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.name")), wd_account_name(account)))
			wi_array_add_data(array, dictionary);
	}

	wi_array_add_data(array, account->values);
	
	result = wi_plist_write_instance_to_file(array, wd_groups_path);
		
	if(!result)
		wi_log_err(WI_STR("Could not write %@: %m"));

	wi_recursive_lock_unlock(wd_groups_lock);
	
	return result;
}



wi_boolean_t wd_accounts_delete_user(wi_string_t *name) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_array_t				*array;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			result = false;
	
	wi_recursive_lock_lock(wd_users_lock);
	
	instance	= wi_plist_read_instance_from_file(wd_users_path);
	array		= wi_array();
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		if(!wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.name")), name))
			wi_array_add_data(array, dictionary);
	}

	result = wi_plist_write_instance_to_file(array, wd_users_path);
		
	if(!result)
		wi_log_err(WI_STR("Could not write %@: %m"));

	wi_recursive_lock_unlock(wd_users_lock);

	return result;
}



wi_boolean_t wd_accounts_delete_group(wi_string_t *name) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_array_t				*array;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			result = false;
	
	wi_recursive_lock_lock(wd_groups_lock);
	
	instance	= wd_accounts_instance_at_path(wd_groups_path);
	array		= wi_array();
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		if(!wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.name")), name))
			wi_array_add_data(array, dictionary);
	}

	result = wi_plist_write_instance_to_file(array, wd_groups_path);
		
	if(!result)
		wi_log_err(WI_STR("Could not write %@: %m"));

	wi_recursive_lock_unlock(wd_groups_lock);

	return result;
}



wi_boolean_t wd_accounts_clear_group(wi_string_t *name) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_array_t				*groups;
	wi_runtime_instance_t	*instance;
	wi_uinteger_t			i, count;
	wi_boolean_t			result = false;
	
	wi_recursive_lock_lock(wd_users_lock);
	
	instance	= wd_accounts_instance_at_path(wd_users_path);
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		if(wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.group")), name))
			wi_dictionary_remove_data_for_key(dictionary, WI_STR("wired.account.group"));
		
		groups = wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.groups"));
		
		if(groups) {
			count = wi_array_count(groups);
			
			for(i = 0; i < count; i++) {
				if(wi_is_equal(WI_ARRAY(groups, i), name)) {
					wi_array_remove_data_at_index(groups, i);
					
					i--;
					count--;
				}
			}
			
			if(wi_array_count(groups) == 0)
				wi_dictionary_remove_data_for_key(dictionary, WI_STR("wired.account.groups"));
		}
	}

	result = wi_plist_write_instance_to_file(instance, wd_users_path);
		
	if(!result)
		wi_log_err(WI_STR("Could not write %@: %m"));

	wi_recursive_lock_unlock(wd_users_lock);

	return result;
}



void wd_accounts_update_login_time(wd_account_t *account) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_array_t				*array;
	wi_runtime_instance_t	*instance;
	
	wi_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.login_time"));
	
	wi_recursive_lock_lock(wd_users_lock);
	
	instance	= wd_accounts_instance_at_path(wd_users_path);
	array		= wi_array();
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		if(!wi_is_equal(wi_dictionary_data_for_key(dictionary, WI_STR("wired.account.name")), wd_account_name(account)))
			wi_array_add_data(array, dictionary);
	}

	wi_array_add_data(array, account->values);
	
	if(!wi_plist_write_instance_to_file(array, wd_users_path))
		wi_log_err(WI_STR("Could not write %@: %m"));

	wi_recursive_lock_unlock(wd_users_lock);
}



void wd_accounts_reload_user_account(wi_string_t *name) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wd_account_t		*account;

	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		account = wd_user_account(user);
		
		if(account && wi_is_equal(wd_account_name(account), name))
			wd_accounts_reload_account(user, account);
	}

	wi_dictionary_unlock(wd_users);
}



void wd_accounts_reload_group_account(wi_string_t *name) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wd_account_t		*account;

	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		account = wd_user_account(user);
		
		if(account && wi_is_equal(wd_account_group(account), name))
			wd_accounts_reload_account(user, account);
	}

	wi_dictionary_unlock(wd_users);
}



void wd_accounts_reload_all_accounts(void) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wd_account_t		*account;
	
	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);

	while((user = wi_enumerator_next_data(enumerator))) {
		account = wd_user_account(user);
		
		if(account)
			wd_accounts_reload_account(user, account);
	}

	wi_dictionary_unlock(wd_users);
}



#pragma mark -

static wi_enumerator_t * wd_accounts_instance_at_path(wi_string_t *path) {
	wi_runtime_instance_t		*instance;
	
	instance = wi_plist_read_instance_from_file(path);
	
	if(!instance || wi_runtime_id(instance) != wi_array_runtime_id())
		instance = wi_array();
	
	return instance;
}



static wi_array_t * wd_accounts_convert_accounts(wi_string_t *path, wi_array_t *fields) {
	wi_file_t					*file;
	wi_string_t					*string, *name, *value;
	wi_array_t					*users, *array;
	wi_dictionary_t				*field, *dictionary;
	wi_runtime_instance_t		*instance;
	wi_uinteger_t				i, count;
	wd_account_field_type_t		type;
	
	users		= wi_array();
	count		= wi_array_count(fields);
	file		= wi_file_for_reading(path);
	
	if(!file)
		return NULL;
	
	while((string = wi_file_read_config_line(file))) {
		array = wi_string_components_separated_by_string(string, WI_STR(":"));
		
		if(wi_array_count(array) > 0) {
			dictionary = wi_dictionary();
			
			for(i = 0; i < count; i++) {
				if(i < wi_array_count(array)) {
					name		= WI_ARRAY(fields, i);
					field		= wi_dictionary_data_for_key(wd_account_fields, name);
					type		= wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)));
					value		= WI_ARRAY(array, i);
					instance	= NULL;
					
					switch(type) {
						case WD_ACCOUNT_FIELD_STRING:
							instance = value;
							
							if(wi_is_equal(name, WI_STR("wired.account.password")) && wi_string_length(instance) == 0)
								instance = wi_string_sha1(WI_STR(""));
							break;

						case WD_ACCOUNT_FIELD_DATE:
							instance = wi_date_with_rfc3339_string(wi_string_by_replacing_string_with_string(value, WI_STR(";"), WI_STR(":"), 0));
							break;

						case WD_ACCOUNT_FIELD_NUMBER:
							instance = wi_number_with_integer(wi_string_integer(value));
							break;

						case WD_ACCOUNT_FIELD_BOOLEAN:
							instance = wi_number_with_bool(wi_string_integer(value));
							break;

						case WD_ACCOUNT_FIELD_LIST:
							instance = wi_string_components_separated_by_string(value, WI_STR(","));
							
							if(wi_array_count(instance) == 1 && wi_string_length(WI_ARRAY(instance, 0)) == 0)
								instance = NULL;
							break;
					}
					
					if(instance)
						wi_dictionary_set_data_for_key(dictionary, instance, name);
				}
			}
			
			if(wi_dictionary_count(dictionary) > 0)
				wi_array_add_data(users, dictionary);
		}
	}
	
	if(!wi_plist_write_instance_to_file(users, path))
		wi_log_err(WI_STR("Could not write %@: %m"));
	
	return users;
}



static void wd_accounts_reload_account(wd_user_t *user, wd_account_t *account) {
	wd_account_t	*new_account;
	wi_boolean_t	admin, new_admin;
	
	new_account = wd_accounts_read_user_and_group(wd_account_name(account));
	
	if(!new_account)
		return;
	
	wd_user_set_account(user, new_account);
	
	admin = wd_user_is_admin(user);
	new_admin = (wd_account_user_kick_users(new_account) || wd_account_user_ban_users(new_account));
	wd_user_set_admin(user, new_admin);
	
	if(admin != new_admin)
		wd_user_broadcast_status(user);
	
	if(!wd_account_log_view_log(new_account) && wd_user_is_subscribed_log(user, NULL))
		wd_user_unsubscribe_log(user);
	
	if(!wd_account_file_list_files(new_account) && wi_set_count(wd_user_subscribed_paths(user)) > 0)
		wd_user_unsubscribe_paths(user);
	
	wd_user_send_message(user, wd_account_privileges_message(new_account));
}



#pragma mark -

void wd_accounts_reply_user_list(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_runtime_instance_t	*instance;
	wi_p7_message_t			*reply;
	wd_account_t			*account;

	wi_recursive_lock_lock(wd_users_lock);
	
	instance	= wd_accounts_instance_at_path(wd_users_path);
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		account = wd_account_init_with_values(wd_account_alloc(), dictionary);
		reply = wi_p7_message_with_name(WI_STR("wired.account.user_list"), wd_p7_spec);
		wd_account_write_to_message(account, WD_ACCOUNT_FIELD_USER_LIST, reply);
		wd_user_reply_message(user, reply, message);
		wi_release(account);
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.user_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	wi_recursive_lock_unlock(wd_users_lock);
}



void wd_accounts_reply_group_list(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_runtime_instance_t	*instance;
	wi_p7_message_t			*reply;
	wd_account_t			*account;

	wi_recursive_lock_lock(wd_groups_lock);
	
	instance	= wd_accounts_instance_at_path(wd_groups_path);
	enumerator	= wi_array_data_enumerator(instance);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		account = wd_account_init_with_values(wd_account_alloc(), dictionary);
		reply = wi_p7_message_with_name(WI_STR("wired.account.group_list"), wd_p7_spec);
		wd_account_write_to_message(account, WD_ACCOUNT_FIELD_GROUP_LIST, reply);
		wd_user_reply_message(user, reply, message);
		wi_release(account);
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.group_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	wi_recursive_lock_unlock(wd_groups_lock);
}



#pragma mark -

wd_account_t * wd_account_alloc(void) {
	return wi_runtime_create_instance(wd_account_runtime_id, sizeof(wd_account_t));
}



static wd_account_t * wd_account_init_with_values(wd_account_t *account, wi_dictionary_t *values) {
	account->values = wi_retain(values);
	
	return account;
}



wd_account_t * wd_account_init_with_message(wd_account_t *account, wi_p7_message_t *message) {
	account->values = wi_dictionary_init(wi_dictionary_alloc());

	wd_account_read_from_message(account, message);
	
	return account;
}



static wi_string_t * wd_account_description(wi_runtime_instance_t *instance) {
	wd_account_t		*account = instance;
	
	return wi_description(account->values);
}



static void wd_account_dealloc(wi_runtime_instance_t *instance) {
	wd_account_t		*account = instance;
	
	wi_release(account->values);
}



#pragma mark -

static void wd_account_read_from_message(wd_account_t *account, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_string_t				*name;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			required;
	
	enumerator = wi_dictionary_key_enumerator(wd_account_fields);
			
	while((name = wi_enumerator_next_data(enumerator))) {
		field		= wi_dictionary_data_for_key(wd_account_fields, name);
		required	= wi_number_bool(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_REQUIRED)));

		switch(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
			case WD_ACCOUNT_FIELD_STRING:
				instance = wi_p7_message_string_for_name(message, name);
				
				if(instance)
					wi_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_DATE:
				instance = wi_p7_message_date_for_name(message, name);
				
				if(instance)
					wi_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_NUMBER:
			case WD_ACCOUNT_FIELD_BOOLEAN:
				instance = wi_p7_message_number_for_name(message, name);
				
				if(instance)
					wi_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_LIST:
				instance = wi_p7_message_list_for_name(message, name);
				
				if(instance)
					wi_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_dictionary_remove_data_for_key(account->values, name);
				break;
		}
	}
}



static void wd_account_write_to_message(wd_account_t *account, wi_uinteger_t type, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_string_t				*name;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			required;
	
	enumerator = wi_dictionary_key_enumerator(wd_account_fields);
	
	while((name = wi_enumerator_next_data(enumerator))) {
		field = wi_dictionary_data_for_key(wd_account_fields, name);

		if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & type) {
			instance = wi_dictionary_data_for_key(account->values, name);
			
			if(type == WD_ACCOUNT_FIELD_PRIVILEGE)
				required = true;
			else
				required = wi_number_bool(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_REQUIRED)));
			
			switch(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
				case WD_ACCOUNT_FIELD_STRING:
					if(!instance && required)
						instance = WI_STR("");
					
					if(instance)
						wi_p7_message_set_string_for_name(message, instance, name);
					break;

				case WD_ACCOUNT_FIELD_DATE:
					if(!instance && required)
						instance = wi_date_with_time(0);
					
					if(instance)
						wi_p7_message_set_date_for_name(message, instance, name);
					break;

				case WD_ACCOUNT_FIELD_NUMBER:
				case WD_ACCOUNT_FIELD_BOOLEAN:
					if(!instance && required)
						instance = wi_number_with_integer(0);
					
					if(instance)
						wi_p7_message_set_number_for_name(message, instance, name);
					break;

				case WD_ACCOUNT_FIELD_LIST:
					if(!instance && required)
						instance = wi_array();
					
					if(instance)
						wi_p7_message_set_list_for_name(message, instance, name);
					break;
			}
		}
	}
}



#pragma mark -

void wd_account_update_from_message(wd_account_t *account, wi_p7_message_t *message) {
	wd_account_read_from_message(account, message);
}



#pragma mark -

void wd_account_reply_user_account(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.user"), wd_p7_spec);
	wd_account_write_to_message(account, WD_ACCOUNT_FIELD_USER, reply);
	wd_user_reply_message(user, reply, message);
}



void wd_account_reply_group_account(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.group"), wd_p7_spec);
	wd_account_write_to_message(account, WD_ACCOUNT_FIELD_GROUP, reply);
	wd_user_reply_message(user, reply, message);
}



wi_p7_message_t * wd_account_privileges_message(wd_account_t *account) {
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(WI_STR("wired.account.privileges"), wd_p7_spec);
	wd_account_write_to_message(account, WD_ACCOUNT_FIELD_PRIVILEGE, message);
	
	return message;
}



#pragma mark -

wi_boolean_t wd_account_check_privileges(wd_account_t *account, wd_user_t *user) {
	wi_enumerator_t				*enumerator;
	wi_dictionary_t				*field;
	wi_string_t					*name;
	wi_runtime_instance_t		*instance1, *instance2;
	wd_account_t				*user_account;
	wd_account_field_type_t		type;
	wi_integer_t				integer_value1, integer_value2;
	wi_boolean_t				boolean_value1, boolean_value2;
	
	user_account = wd_user_account(user);
	
	if(!wd_account_account_raise_account_privileges(user_account)) {
		enumerator = wi_dictionary_key_enumerator(wd_account_fields);
			
		while((name = wi_enumerator_next_data(enumerator))) {
			field = wi_dictionary_data_for_key(wd_account_fields, name);
			
			if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & WD_ACCOUNT_FIELD_PRIVILEGE) {
				type		= wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)));
				instance1	= wi_dictionary_data_for_key(user_account->values, name);
				instance2	= wi_dictionary_data_for_key(account->values, name);
				
				switch(type) {
					case WD_ACCOUNT_FIELD_STRING:
					case WD_ACCOUNT_FIELD_DATE:
					case WD_ACCOUNT_FIELD_LIST:
						break;
					
					case WD_ACCOUNT_FIELD_NUMBER:
						integer_value1 = instance1 ? wi_number_integer(instance1) : 0;
						integer_value2 = instance2 ? wi_number_integer(instance2) : 0;
						
						if(integer_value1 > 0 && (integer_value2 > integer_value1 || integer_value2 == 0))
							return false;
						break;
					
					case WD_ACCOUNT_FIELD_BOOLEAN:
						boolean_value1 = instance1 ? wi_number_bool(instance1) : false;
						boolean_value2 = instance2 ? wi_number_bool(instance2) : false;
						
						if(boolean_value1 && !boolean_value2)
							return false;
						break;
				}
			}
		}
			
		instance1 = wi_dictionary_data_for_key(user_account->values, WI_STR("wired.account.files"));
		instance2 = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.files"));
		
		if(!instance1)
			instance1 = WI_STR("");
		
		if(!instance2)
			instance2 = WI_STR("");
		
		if(!wi_string_has_prefix(instance2, instance1))
			return false;
		
		if(!wi_is_equal(wi_dictionary_data_for_key(user_account->values, WI_STR("wired.account.group")),
						wi_dictionary_data_for_key(account->values, WI_STR("wired.account.group"))))
		   return false;
		
		if(!wi_is_equal(wi_dictionary_data_for_key(user_account->values, WI_STR("wired.account.groups")),
						wi_dictionary_data_for_key(account->values, WI_STR("wired.account.groups"))))
		   return false;
	}
	
	return true;
}



#pragma mark -

#define WD_ACCOUNT_STRING_ACCESSOR(name, field)									\
	wi_string_t * (name)(wd_account_t *account) {								\
		wi_string_t		*string;												\
																				\
		if((string = wi_dictionary_data_for_key((account)->values, (field))))	\
			return string;														\
																				\
		return WI_STR("");														\
	}

#define WD_ACCOUNT_DATE_ACCESSOR(name, field)									\
	wi_date_t * (name)(wd_account_t *account) {									\
		return wi_dictionary_data_for_key((account)->values, (field));			\
	}

#define WD_ACCOUNT_BOOLEAN_ACCESSOR(name, field)								\
	wi_boolean_t (name)(wd_account_t *account) {								\
		wi_number_t		*number;												\
																				\
		if((number = wi_dictionary_data_for_key((account)->values, (field))))	\
			return wi_number_bool(number);										\
																				\
		return false;															\
	}

#define WD_ACCOUNT_NUMBER_ACCESSOR(name, field)									\
	wi_uinteger_t (name)(wd_account_t *account) {								\
		wi_number_t		*number;												\
																				\
		if((number = wi_dictionary_data_for_key((account)->values, (field))))	\
			return wi_number_integer(number);									\
																				\
		return 0;																\
	}

#define WD_ACCOUNT_LIST_ACCESSOR(name, field)									\
	wi_array_t * (name)(wd_account_t *account) {								\
		wi_array_t		*array;													\
																				\
		if((array = wi_dictionary_data_for_key((account)->values, (field))))	\
			return array;														\
																				\
		return wi_array();														\
	}

WD_ACCOUNT_STRING_ACCESSOR(wd_account_name, WI_STR("wired.account.name"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_full_name, WI_STR("wired.account.full_name"))
WD_ACCOUNT_DATE_ACCESSOR(wd_account_creation_time, WI_STR("wired.account.creation_time"))
WD_ACCOUNT_DATE_ACCESSOR(wd_account_modification_time, WI_STR("wired.account.modification_time"))
WD_ACCOUNT_DATE_ACCESSOR(wd_account_login_time, WI_STR("wired.account.login_time"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_edited_by, WI_STR("wired.account.edited_by"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_password, WI_STR("wired.account.password"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_group, WI_STR("wired.account.group"))
WD_ACCOUNT_LIST_ACCESSOR(wd_account_groups, WI_STR("wired.account.groups"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_files, WI_STR("wired.account.files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_cannot_set_nick, WI_STR("wired.account.files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_get_info, WI_STR("wired.account.user.get_info"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_kick_users, WI_STR("wired.account.user.kick_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_ban_users, WI_STR("wired.account.user.ban_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_cannot_be_disconnected, WI_STR("wired.account.user.cannot_be_disconnected"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_chat_set_topic, WI_STR("wired.account.chat.set_topic"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_chat_create_chats, WI_STR("wired.account.chat.create_chats"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_message_send_messages, WI_STR("wired.account.message.send_messages"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_message_broadcast, WI_STR("wired.account.message.broadcast"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_read_boards, WI_STR("wired.account.board.read_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_add_boards, WI_STR("wired.account.board.add_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_move_boards, WI_STR("wired.account.board.move_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_rename_boards, WI_STR("wired.account.board.rename_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_delete_boards, WI_STR("wired.account.board.delete_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_set_permissions, WI_STR("wired.account.board.set_permissions"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_add_threads, WI_STR("wired.account.board.add_threads"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_move_threads, WI_STR("wired.account.board.move_threads"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_delete_threads, WI_STR("wired.account.board.delete_threads"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_add_posts, WI_STR("wired.account.board.add_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_edit_own_posts, WI_STR("wired.account.board.edit_own_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_edit_all_posts, WI_STR("wired.account.board.edit_all_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_delete_posts, WI_STR("wired.account.board.delete_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_list_files, WI_STR("wired.account.file.list_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_get_info, WI_STR("wired.account.file.get_info"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_create_directories, WI_STR("wired.account.file.create_directories"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_create_links, WI_STR("wired.account.file.create_links"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_move_files, WI_STR("wired.account.file.move_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_rename_files, WI_STR("wired.account.file.rename_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_type, WI_STR("wired.account.file.set_type"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_comment, WI_STR("wired.account.file.set_comment"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_permissions, WI_STR("wired.account.file.set_permissions"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_executable, WI_STR("wired.account.file.set_executable"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_delete_files, WI_STR("wired.account.file.delete_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_access_all_dropboxes, WI_STR("wired.account.file.access_all_dropboxes"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_file_recursive_list_depth_limit, WI_STR("wired.account.file.recursive_list_depth_limit"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_transfer_download_files, WI_STR("wired.account.transfer.download_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_transfer_upload_files, WI_STR("wired.account.transfer.upload_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_transfer_upload_directories, WI_STR("wired.account.transfer.upload_directories"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_transfer_upload_anywhere, WI_STR("wired.account.transfer.upload_anywhere"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_transfer_download_limit, WI_STR("wired.account.transfer.download_limit"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_transfer_upload_limit, WI_STR("wired.account.transfer.upload_limit"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_transfer_download_speed_limit, WI_STR("wired.account.transfer.download_speed_limit"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_transfer_upload_speed_limit, WI_STR("wired.account.transfer.upload_speed_limit"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_change_password, WI_STR("wired.account.account.change_password"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_list_accounts, WI_STR("wired.account.account.list_accounts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_read_accounts, WI_STR("wired.account.account.read_accounts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_create_users, WI_STR("wired.account.account.create_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_edit_users, WI_STR("wired.account.account.edit_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_delete_users, WI_STR("wired.account.account.delete_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_create_groups, WI_STR("wired.account.account.create_groups"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_edit_groups, WI_STR("wired.account.account.edit_groups"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_delete_groups, WI_STR("wired.account.account.delete_groups"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_raise_account_privileges, WI_STR("wired.account.account.raise_account_privileges"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_get_users, WI_STR("wired.account.user.get_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_log_view_log, WI_STR("wired.account.log.view_log"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_settings_get_settings, WI_STR("wired.account.settings.get_settings"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_settings_set_settings, WI_STR("wired.account.settings.set_settings"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_banlist_get_bans, WI_STR("wired.account.banlist.get_bans"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_banlist_add_bans, WI_STR("wired.account.banlist.add_bans"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_banlist_delete_bans, WI_STR("wired.account.banlist.delete_bans"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_tracker_list_servers, WI_STR("wired.account.tracker.list_servers"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_tracker_register_servers, WI_STR("wired.account.tracker.register_servers"))
