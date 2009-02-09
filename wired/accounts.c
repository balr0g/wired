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

#define WD_ACCOUNT_FIELD_NAME				"name"
#define WD_ACCOUNT_FIELD_TYPE				"type"
#define WD_ACCOUNT_FIELD_STRING					0
#define WD_ACCOUNT_FIELD_DATE					1
#define WD_ACCOUNT_FIELD_NUMBER					2
#define WD_ACCOUNT_FIELD_BOOLEAN				3
#define WD_ACCOUNT_FIELD_LIST					4
#define WD_ACCOUNT_FIELD_ACCOUNT			"account"
#define WD_ACCOUNT_FIELD_USER					(1 << 0)
#define WD_ACCOUNT_FIELD_GROUP					(1 << 1)
#define WD_ACCOUNT_FIELD_PRIVILEGE				(1 << 2)
#define WD_ACCOUNT_FIELD_USER_AND_GROUP			(WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_GROUP)
#define WD_ACCOUNT_FIELD_ALL					(WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_GROUP | WD_ACCOUNT_FIELD_PRIVILEGE)
#define WD_ACCOUNT_FIELD_REQUIRED			"required"


static wi_boolean_t							wd_accounts_delete_from_file(wi_file_t *, wi_string_t *);
static void									wd_accounts_reload_account(wd_user_t *, wd_account_t *);

static wd_account_t *						wd_account_init_with_type(wd_account_t *, wi_uinteger_t, wi_array_t *);
static void									wd_account_dealloc(wi_runtime_instance_t *);

static void									wd_account_read_from_message(wd_account_t *, wi_p7_message_t *);
static void									wd_account_write_to_message(wd_account_t *, wi_uinteger_t, wi_p7_message_t *);
static wi_array_t *							wd_account_array_with_type(wd_account_t *, wi_uinteger_t);

static wi_string_t							*wd_users_path;
static wi_string_t							*wd_groups_path;

static wi_recursive_lock_t					*wd_users_lock;
static wi_recursive_lock_t					*wd_groups_lock;

static wi_array_t							*wd_account_fields;

static wi_runtime_id_t						wd_account_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t					wd_account_runtime_class = {
	"wd_account_t",
	wd_account_dealloc,
	NULL,
	NULL,
	NULL,
	NULL
};


void wd_accounts_init(void) {
	wd_account_runtime_id = wi_runtime_register_class(&wd_account_runtime_class);

	wd_users_path = WI_STR("users");
	wd_groups_path = WI_STR("groups");
	
	wd_users_lock = wi_recursive_lock_init(wi_recursive_lock_alloc());
	wd_groups_lock = wi_recursive_lock_init(wi_recursive_lock_alloc());
	
	wd_account_fields = wi_array_init_with_data(wi_array_alloc(),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.name"),								WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_STRING),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER_AND_GROUP),					WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.password"),							WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_STRING),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER),							WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.group"),								WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_STRING),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER),							WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.user.get_info"),						WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.message.broadcast"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.add_posts"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.delete_posts"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.transfer.download_files"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.transfer.upload_files"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.transfer.upload_anywhere"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.create_directories"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.move_files"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.delete_files"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.access_all_dropboxes"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.account.create_accounts"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.account.edit_accounts"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.account.delete_accounts"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.account.raise_account_privileges"),	WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.user.kick_users"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.user.ban_users"),						WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.user.cannot_be_disconnected"),		WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.transfer.download_speed_limit"),		WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_NUMBER),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.transfer.upload_speed_limit"),		WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_NUMBER),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.transfer.download_limit"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_NUMBER),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.transfer.upload_limit"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_NUMBER),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.chat.set_topic"),						WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.files"),								WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_STRING),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER_AND_GROUP),					WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.full_name"),							WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_STRING),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER),							WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.creation_time"),						WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_DATE),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER_AND_GROUP),					WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.modification_time"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_DATE),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER_AND_GROUP),					WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.login_time"),							WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_DATE),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER_AND_GROUP),					WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.edited_by"),							WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_STRING),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER_AND_GROUP),					WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.groups"),								WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_LIST),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_USER),							WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			wi_number_with_bool(true),									WI_STR(WD_ACCOUNT_FIELD_REQUIRED),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.chat.create_chats"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.message.send_messages"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.read_boards"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.list_files"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.get_info"),						WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.create_links"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.rename_files"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.set_type"),						WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.set_comment"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.set_permissions"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.set_executable"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.file.recursive_list_depth_limit"),	WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_NUMBER),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.transfer.upload_directories"),		WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.account.change_password"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.account.list_accounts"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.account.read_accounts"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.user.cannot_set_nick"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.user.get_users"),						WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.log.view_log"),						WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.settings.get_settings"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.settings.set_settings"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.banlist.get_bans"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.banlist.add_bans"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.banlist.delete_bans"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.tracker.list_servers"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.tracker.register_servers"),			WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.add_boards"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.move_boards"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.rename_boards"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.delete_boards"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.set_permissions"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.add_threads"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.move_threads"),					WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.delete_threads"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.edit_own_posts"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		wi_dictionary_with_data_and_keys(
			WI_STR("wired.account.board.edit_all_posts"),				WI_STR(WD_ACCOUNT_FIELD_NAME),
			WI_INT32(WD_ACCOUNT_FIELD_BOOLEAN),							WI_STR(WD_ACCOUNT_FIELD_TYPE),
			WI_INT32(WD_ACCOUNT_FIELD_ALL),								WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),
			NULL),
		NULL);
}



#pragma mark -

wd_account_t * wd_accounts_read_user_and_group(wi_string_t *name) {
	wi_enumerator_t		*enumerator;
	wi_dictionary_t		*field;
	wi_string_t			*field_name;
	wd_account_t		*user, *group;
	
	user = wd_accounts_read_user(name);
	
	if(!user)
		return NULL;
	
	if(wi_string_length(wd_account_group(user)) > 0) {
		group = wd_accounts_read_group(wd_account_group(user));
		
		if(group) {
			enumerator = wi_array_data_enumerator(wd_account_fields);
			
			while((field = wi_enumerator_next_data(enumerator))) {
				if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & WD_ACCOUNT_FIELD_PRIVILEGE) {
					field_name = wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_NAME));

					wi_dictionary_set_data_for_key(user->values, wi_dictionary_data_for_key(group->values, field_name), field_name);
				}
			}
		}
	}
	
	return user;
}



wd_account_t * wd_accounts_read_user(wi_string_t *name) {
	wi_file_t		*file;
	wi_array_t		*array;
	wi_string_t		*string;
	wd_account_t	*account = NULL;
	
	wi_recursive_lock_lock(wd_users_lock);
	
	file = wi_file_for_reading(wd_users_path);
	
	if(file) {
		while((string = wi_file_read_config_line(file))) {
			array = wi_string_components_separated_by_string(string, WI_STR(":"));
			
			if(wi_array_count(array) > 0 && wi_is_equal(WI_ARRAY(array, 0), name)) {
				account = wd_account_init_with_type(wd_account_alloc(), WD_ACCOUNT_FIELD_USER, array);
				
				break;
			}
		}
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_users_path);
	}
	
	wi_recursive_lock_unlock(wd_users_lock);
	
	return wi_autorelease(account);
}



wd_account_t * wd_accounts_read_group(wi_string_t *name) {
	wi_file_t		*file;
	wi_array_t		*array;
	wi_string_t		*string;
	wd_account_t	*account = NULL;
	
	wi_recursive_lock_lock(wd_groups_lock);
	
	file = wi_file_for_reading(wd_groups_path);
	
	if(file) {
		while((string = wi_file_read_config_line(file))) {
			array = wi_string_components_separated_by_string(string, WI_STR(":"));
			
			if(wi_array_count(array) > 0 && wi_is_equal(WI_ARRAY(array, 0), name)) {
				account = wd_account_init_with_type(wd_account_alloc(), WD_ACCOUNT_FIELD_GROUP, array);
				
				break;
			}
		}
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_groups_path);
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
	wi_boolean_t		result = false;
	
	wi_dictionary_set_data_for_key(account->values, password, WI_STR("wired.account.password"));
	
	wi_recursive_lock_lock(wd_users_lock);
	
	if(wd_accounts_delete_user(wd_account_name(account))) {
		if(wd_accounts_create_user(account))
			result = true;
	}
	
	wi_recursive_lock_unlock(wd_users_lock);

	return result;
}



wi_boolean_t wd_accounts_create_user(wd_account_t *account) {
	wi_file_t		*file;
	wi_array_t		*array;
	wi_boolean_t	result = false;
	
	wi_recursive_lock_lock(wd_users_lock);

	file = wi_file_for_updating(wd_users_path);

	if(file) {
		array = wd_account_array_with_type(account, WD_ACCOUNT_FIELD_USER);

		wi_file_write_format(file, WI_STR("%@\n"), wi_array_components_joined_by_string(array, WI_STR(":")));
		
		result = true;
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_users_path);
	}

	wi_recursive_lock_unlock(wd_users_lock);
	
	return result;
}



wi_boolean_t wd_accounts_create_group(wd_account_t *account) {
	wi_file_t		*file;
	wi_array_t		*array;
	wi_boolean_t	result = false;
	
	wi_recursive_lock_lock(wd_groups_lock);

	file = wi_file_for_updating(wd_groups_path);

	if(file) {
		array = wd_account_array_with_type(account, WD_ACCOUNT_FIELD_GROUP);

		wi_file_write_format(file, WI_STR("%@\n"), wi_array_components_joined_by_string(array, WI_STR(":")));
		
		result = true;
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_groups_path);
	}

	wi_recursive_lock_unlock(wd_groups_lock);
	
	return result;
}



wi_boolean_t wd_accounts_edit_user(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_boolean_t	result = false;
	
	wd_account_read_from_message(account, message);
	
	wi_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.modification_time"));
	wi_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	wi_recursive_lock_lock(wd_users_lock);
	
	if(wd_accounts_delete_user(wd_account_name(account))) {
		if(wd_accounts_create_user(account))
			result = true;
	}
	
	wi_recursive_lock_unlock(wd_users_lock);
	
	return result;
}



wi_boolean_t wd_accounts_edit_group(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_boolean_t	result = false;
	
	wd_account_read_from_message(account, message);
	
	wi_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.modification_time"));
	wi_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	wi_recursive_lock_lock(wd_groups_lock);
	
	if(wd_accounts_delete_group(wd_account_name(account))) {
		if(wd_accounts_create_group(account))
			result = true;
	}
	
	wi_recursive_lock_unlock(wd_groups_lock);
	
	return result;
}



wi_boolean_t wd_accounts_delete_user(wi_string_t *name) {
	wi_file_t		*file;
	wi_boolean_t	result = false;
	
	wi_recursive_lock_lock(wd_users_lock);
	
	file = wi_file_for_updating(wd_users_path);

	if(file)
		result = wd_accounts_delete_from_file(file, name);
	else
		wi_log_err(WI_STR("Could not open %@: %m"), wd_users_path);

	wi_recursive_lock_unlock(wd_users_lock);
	
	return result;
}



wi_boolean_t wd_accounts_delete_group(wi_string_t *name) {
	wi_file_t		*file;
	wi_boolean_t	result = false;
	
	wi_recursive_lock_lock(wd_groups_lock);
	
	file = wi_file_for_updating(wd_groups_path);

	if(file)
		result = wd_accounts_delete_from_file(file, name);
	else
		wi_log_err(WI_STR("Could not open %@: %m"), wd_groups_path);

	wi_recursive_lock_unlock(wd_groups_lock);
	
	return result;
}



wi_boolean_t wd_accounts_clear_group(wi_string_t *name) {
	wi_file_t		*file, *tmpfile = NULL;
	wi_array_t		*array;
	wi_string_t		*string;
	wi_boolean_t	result = false;
	
	wi_recursive_lock_lock(wd_users_lock);
	
	file = wi_file_for_updating(wd_users_path);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_users_path);

		goto end;
	}

	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_err(WI_STR("Could not create a temporary file: %m"));

		goto end;
	}
	
	while((string = wi_file_read_line(file)))
		wi_file_write_format(tmpfile, WI_STR("%@\n"), string);
	
	wi_file_truncate(file, 0);
	wi_file_seek(tmpfile, 0);
	
	while((string = wi_file_read_line(tmpfile))) {
		if(wi_string_length(string) > 0 && !wi_string_has_prefix(string, WI_STR("#"))) {
			array = wi_string_components_separated_by_string(string, WI_STR(":"));
			
			if(wi_array_count(array) > 2 && wi_is_equal(WI_ARRAY(array, 2), name)) {
				wi_array_replace_data_at_index(array, WI_STR(""), 2);
				
				string = wi_array_components_joined_by_string(array, WI_STR(":"));
			}
		}
			
		wi_file_write_format(file, WI_STR("%@\n"), string);
	}
	
end:
	wi_recursive_lock_unlock(wd_users_lock);
	
	return result;
}



void wd_accounts_update_login_time(wd_account_t *account) {
	wi_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.login_time"));
	
	wi_recursive_lock_lock(wd_users_lock);
	
	if(wd_accounts_delete_user(wd_account_name(account)))
		wd_accounts_create_user(account);
	
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

static wi_boolean_t wd_accounts_delete_from_file(wi_file_t *file, wi_string_t *name) {
	wi_file_t		*tmpfile;
	wi_array_t		*array;
	wi_string_t		*string;
	wi_boolean_t	result = false;
	
	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_err(WI_STR("Could not create a temporary file: %m"));

		return false;
	}
	
	while((string = wi_file_read_line(file))) {
		if(wi_string_length(string) == 0 || wi_string_has_prefix(string, WI_STR("#"))) {
			wi_file_write_format(tmpfile, WI_STR("%@\n"), string);
		} else {
			array = wi_string_components_separated_by_string(string, WI_STR(":"));
			
			if(wi_array_count(array) > 0 && wi_is_equal(WI_ARRAY(array, 0), name))
				result = true;
			else
				wi_file_write_format(tmpfile, WI_STR("%@\n"), string);
		}
	}
	
	wi_file_truncate(file, 0);
	wi_file_seek(tmpfile, 0);
	
	while((string = wi_file_read(tmpfile, WI_FILE_BUFFER_SIZE)))
		wi_file_write_format(file, WI_STR("%@"), string);
	
	return result;
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
	wi_p7_message_t		*reply;
	wi_file_t			*file;
	wi_string_t			*string;
	wi_uinteger_t		index;

	wi_recursive_lock_lock(wd_users_lock);
	
	file = wi_file_for_reading(wd_users_path);

	if(file) {
		while((string = wi_file_read_config_line(file))) {
			index = wi_string_index_of_string(string, WI_STR(":"), 0);
			
			if(index != WI_NOT_FOUND && index > 0) {
				wi_string_delete_characters_from_index(string, index);
				
				reply = wi_p7_message_with_name(WI_STR("wired.account.user_list"), wd_p7_spec);
				wi_p7_message_set_string_for_name(reply, string, WI_STR("wired.account.name"));
				wd_user_reply_message(user, reply, message);
			}
		}
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_users_path);
	}

	reply = wi_p7_message_with_name(WI_STR("wired.account.user_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	wi_recursive_lock_unlock(wd_users_lock);
}



void wd_accounts_reply_group_list(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_file_t			*file;
	wi_string_t			*string;
	wi_uinteger_t		index;

	wi_recursive_lock_lock(wd_groups_lock);
	
	file = wi_file_for_reading(wd_groups_path);

	if(file) {
		while((string = wi_file_read_config_line(file))) {
			index = wi_string_index_of_string(string, WI_STR(":"), 0);
			
			if(index != WI_NOT_FOUND && index > 0) {
				wi_string_delete_characters_from_index(string, index);
				
				reply = wi_p7_message_with_name(WI_STR("wired.account.group_list"), wd_p7_spec);
				wi_p7_message_set_string_for_name(reply, string, WI_STR("wired.account.name"));
				wd_user_reply_message(user, reply, message);
			}
		}
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_groups_path);
	}

	reply = wi_p7_message_with_name(WI_STR("wired.account.group_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	wi_recursive_lock_unlock(wd_groups_lock);
}



#pragma mark -

wd_account_t * wd_account_alloc(void) {
	return wi_runtime_create_instance(wd_account_runtime_id, sizeof(wd_account_t));
}



static wd_account_t * wd_account_init_with_type(wd_account_t *account, wi_uinteger_t type, wi_array_t *array) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_string_t				*name, *value;
	wi_runtime_instance_t	*instance;
	wi_uinteger_t			i = 0, count;
	wi_boolean_t			required;
	
	account->values		= wi_dictionary_init(wi_dictionary_alloc());
	count				= wi_array_count(array);
	enumerator			= wi_array_data_enumerator(wd_account_fields);
	
	while((field = wi_enumerator_next_data(enumerator))) {
		if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & type) {
			name		= wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_NAME));
			value		= (i < count) ? WI_ARRAY(array, i) : NULL;
			required	= (wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_REQUIRED)) != NULL);
			
			switch(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
				case WD_ACCOUNT_FIELD_STRING:
					if(value)
						instance = value;
					else if(required)
						instance = WI_STR("");
					else
						instance = NULL;
					
					if(instance) {
						if(wi_is_equal(name, WI_STR("wired.account.password")) && wi_string_length(instance) == 0)
							instance = wi_string_sha1(WI_STR(""));
						
						wi_dictionary_set_data_for_key(account->values, instance, name);
					}
					break;
					
				case WD_ACCOUNT_FIELD_DATE:
					if(value) {
						instance = wi_date_with_rfc3339_string(wi_string_by_replacing_string_with_string(value, WI_STR(";"), WI_STR(":"), 0));
						
						if(!instance && required)
							instance = wi_date_with_time(0);
					}
					else if(required)
						instance = wi_date_with_time(0);
					else
						instance = NULL;
					
					if(instance)
						wi_dictionary_set_data_for_key(account->values, instance, name);
					break;
					
				case WD_ACCOUNT_FIELD_NUMBER:
					if(value)
						instance = wi_number_with_integer(wi_string_integer(value));
					else if(required)
						instance = wi_number_with_integer(0);
					else
						instance = NULL;

					if(instance)
						wi_dictionary_set_data_for_key(account->values, instance, name);
					break;
					
				case WD_ACCOUNT_FIELD_BOOLEAN:
					if(value)
						instance = wi_number_with_bool(wi_string_integer(value));
					else if(required)
						instance = wi_number_with_bool(false);
					else
						instance = NULL;

					if(instance)
						wi_dictionary_set_data_for_key(account->values, instance, name);
					break;
					
				case WD_ACCOUNT_FIELD_LIST:
					if(value)
						instance = wi_string_components_separated_by_string(value, WI_STR(","));
					else if(required)
						instance = wi_array();
					else
						instance = NULL;

					if(instance)
						wi_dictionary_set_data_for_key(account->values, instance, name);
					break;
			}
		}
		
		i++;
	}
	
	return account;
}



wd_account_t * wd_account_init_with_message(wd_account_t *account, wi_p7_message_t *message) {
	account->values = wi_dictionary_init(wi_dictionary_alloc());

	wd_account_read_from_message(account, message);
	
	return account;
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
	
	enumerator = wi_array_data_enumerator(wd_account_fields);
	
	while((field = wi_enumerator_next_data(enumerator))) {
		name = wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_NAME));
		
		switch(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
			case WD_ACCOUNT_FIELD_STRING:
				instance = wi_p7_message_string_for_name(message, name);
				
				if(instance)
					wi_dictionary_set_data_for_key(account->values, instance, name);
				else
					wi_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_DATE:
				instance = wi_p7_message_date_for_name(message, name);
				
				if(instance)
					wi_dictionary_set_data_for_key(account->values, instance, name);
				else
					wi_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_NUMBER:
			case WD_ACCOUNT_FIELD_BOOLEAN:
				instance = wi_p7_message_number_for_name(message, name);
				
				if(instance)
					wi_dictionary_set_data_for_key(account->values, instance, name);
				else
					wi_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_LIST:
				instance = wi_p7_message_list_for_name(message, name);
				
				if(instance)
					wi_dictionary_set_data_for_key(account->values, instance, name);
				else
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
	
	enumerator = wi_array_data_enumerator(wd_account_fields);
	
	while((field = wi_enumerator_next_data(enumerator))) {
		if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & type) {
			name		= wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_NAME));
			instance	= wi_dictionary_data_for_key(account->values, name);
		
			if(instance) {
				switch(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
					case WD_ACCOUNT_FIELD_STRING:
						wi_p7_message_set_string_for_name(message, instance, name);
						break;

					case WD_ACCOUNT_FIELD_DATE:
						wi_p7_message_set_date_for_name(message, instance, name);
						break;

					case WD_ACCOUNT_FIELD_NUMBER:
					case WD_ACCOUNT_FIELD_BOOLEAN:
						wi_p7_message_set_number_for_name(message, instance, name);
						break;

					case WD_ACCOUNT_FIELD_LIST:
						wi_p7_message_set_list_for_name(message, instance, name);
						break;
				}
			}
		}
	}
}



static wi_array_t * wd_account_array_with_type(wd_account_t *account, wi_uinteger_t type) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_array_t				*array;
	wi_string_t				*name, *value;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			required;
	
	array		= wi_array();
	enumerator	= wi_array_data_enumerator(wd_account_fields);
	
	while((field = wi_enumerator_next_data(enumerator))) {
		if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & type) {
			name		= wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_NAME));
			instance	= wi_dictionary_data_for_key(account->values, name);
			required	= (wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_REQUIRED)) != NULL);
			
			switch(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
				case WD_ACCOUNT_FIELD_STRING:
					if(instance)
						value = instance;
					else
						value = WI_STR("");

					wi_array_add_data(array, value);
					break;

				case WD_ACCOUNT_FIELD_DATE:
					if(instance)
						value = wi_string_by_replacing_string_with_string(wi_date_rfc3339_string(instance), WI_STR(":"), WI_STR(";"), 0);
					else
						value = WI_STR("");
						
					wi_array_add_data(array, value);
					break;

				case WD_ACCOUNT_FIELD_NUMBER:
					if(instance)
						value = wi_string_with_format(WI_STR("%u"), wi_number_integer(instance));
					else
						value = WI_STR("");

					wi_array_add_data(array, value);
					break;
					
				case WD_ACCOUNT_FIELD_BOOLEAN:
					if(instance)
						value = wi_string_with_format(WI_STR("%u"), wi_number_bool(instance));
					else
						value = WI_STR("");

					wi_array_add_data(array, value);
					break;

				case WD_ACCOUNT_FIELD_LIST:
					if(instance)
						value = wi_array_components_joined_by_string(instance, WI_STR(","));
					else
						value = WI_STR("");

					wi_array_add_data(array, value);
					break;
			}
		}
	}
	
	return array;
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
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_string_t				*name;
	wi_runtime_instance_t	*instance1, *instance2;
	wd_account_t			*user_account;
	
	user_account = wd_user_account(user);
	
	if(!wd_account_account_raise_account_privileges(user_account)) {
		enumerator = wi_array_data_enumerator(wd_account_fields);
		
		while((field = wi_enumerator_next_data(enumerator))) {
			if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & WD_ACCOUNT_FIELD_PRIVILEGE) {
				name		= wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_NAME));
				instance1	= wi_dictionary_data_for_key(account->values, name);
				instance2	= wi_dictionary_data_for_key(user_account->values, name);
				
				if(!wi_is_equal(instance1, instance2))
					return false;
			}
		}
			
		instance1 = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.files"));
		instance2 = wi_dictionary_data_for_key(user_account->values, WI_STR("wired.account.files"));
		
		if(!instance1)
			instance1 = WI_STR("");
		
		if(!instance2)
			instance2 = WI_STR("");
		
		if(!wi_string_has_prefix(instance1, instance2))
			return false;
	}
	
	return true;
}



#pragma mark -

wi_string_t * wd_account_name(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.name"));
}



wi_string_t * wd_account_full_name(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.full_name"));
}



wi_date_t * wd_account_creation_time(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.creation_time"));
}



wi_date_t * wd_account_modification_time(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.modification_time"));
}



wi_date_t * wd_account_login_time(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.login_time"));
}



wi_string_t * wd_account_edited_by(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.edited_by"));
}



wi_string_t * wd_account_password(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.password"));
}



wi_string_t * wd_account_group(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.group"));
}



wi_array_t * wd_account_groups(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.groups"));
}



wi_string_t * wd_account_files(wd_account_t *account) {
	return wi_dictionary_data_for_key(account->values, WI_STR("wired.account.files"));
}



wi_boolean_t wd_account_user_cannot_set_nick(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.files"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_user_get_info(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.user.get_info"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_user_kick_users(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.user.kick_users"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_user_ban_users(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.user.ban_users"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_user_cannot_be_disconnected(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.user.cannot_be_disconnected"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_chat_set_topic(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.chat.set_topic"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_chat_create_chats(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.chat.create_chats"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_message_send_messages(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.message.send_messages"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_message_broadcast(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.message.broadcast"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_read_boards(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.read_boards"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_add_boards(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.add_boards"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_move_boards(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.move_boards"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_rename_boards(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.rename_boards"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_delete_boards(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.delete_boards"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_set_permissions(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.set_permissions"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_add_threads(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.add_threads"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_move_threads(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.move_threads"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_delete_threads(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.delete_threads"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_add_posts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.add_posts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_edit_own_posts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.edit_own_posts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_edit_all_posts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.edit_all_posts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_board_delete_posts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.board.delete_posts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_list_files(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.list_files"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_get_info(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.get_info"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_create_directories(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.create_directories"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_create_links(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.create_links"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_move_files(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.move_files"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_rename_files(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.rename_files"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_set_type(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.set_type"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_set_comment(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.set_comment"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_set_permissions(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.set_permissions"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_set_executable(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.set_executable"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_delete_files(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.delete_files"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_file_access_all_dropboxes(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.access_all_dropboxes"))))
		return wi_number_bool(number);

	return false;
}



wi_uinteger_t wd_account_file_recursive_list_depth_limit(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.file.recursive_list_depth_limit"))))
		return wi_number_integer(number);

	return false;
}



wi_boolean_t wd_account_transfer_download_files(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.transfer.download_files"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_transfer_upload_files(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.transfer.upload_files"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_transfer_upload_directories(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.transfer.upload_directories"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_transfer_upload_anywhere(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.transfer.upload_anywhere"))))
		return wi_number_bool(number);

	return false;
}



wi_uinteger_t wd_account_transfer_download_limit(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.transfer.download_limit"))))
		return wi_number_integer(number);

	return false;
}



wi_uinteger_t wd_account_transfer_upload_limit(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.transfer.upload_limit"))))
		return wi_number_integer(number);

	return false;
}



wi_uinteger_t wd_account_transfer_download_speed_limit(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.transfer.download_speed_limit"))))
		return wi_number_integer(number);

	return false;
}



wi_uinteger_t wd_account_transfer_upload_speed_limit(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.transfer.upload_speed_limit"))))
		return wi_number_integer(number);

	return false;
}



wi_boolean_t wd_account_account_change_password(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.account.change_password"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_account_list_accounts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.account.list_accounts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_account_read_accounts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.account.read_accounts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_account_create_accounts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.account.create_accounts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_account_edit_accounts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.account.edit_accounts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_account_delete_accounts(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.account.delete_accounts"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_account_raise_account_privileges(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.account.raise_account_privileges"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_user_get_users(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.user.get_users"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_log_view_log(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.log.view_log"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_settings_get_settings(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.settings.get_settings"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_settings_set_settings(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.settings.set_settings"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_banlist_get_bans(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.banlist.get_bans"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_banlist_add_bans(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.banlist.add_bans"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_banlist_delete_bans(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.banlist.delete_bans"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_tracker_list_servers(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.tracker.list_servers"))))
		return wi_number_bool(number);

	return false;
}



wi_boolean_t wd_account_tracker_register_servers(wd_account_t *account) {
	wi_number_t		*number;

	if((number = wi_dictionary_data_for_key(account->values, WI_STR("wired.account.tracker.register_servers"))))
		return wi_number_bool(number);

	return false;
}
