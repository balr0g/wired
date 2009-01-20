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

static wi_boolean_t					wd_accounts_delete_from_file(wi_file_t *, wi_string_t *);
static void							wd_accounts_reload_account(wd_user_t *, wd_account_t *);
static void							wd_accounts_copy_attributes(wd_account_t *, wd_account_t *);

static void							wd_account_dealloc(wi_runtime_instance_t *);

static wi_string_t *				wd_account_next_string(wi_array_t *, wi_uinteger_t *);
static wi_array_t *					wd_account_next_array(wi_array_t *, wi_uinteger_t *);
static wi_date_t *					wd_account_next_date(wi_array_t *, wi_uinteger_t *);
static wi_boolean_t					wd_account_next_bool(wi_array_t *, wi_boolean_t, wi_uinteger_t *);
static wi_uinteger_t				wd_account_next_uinteger(wi_array_t *, wi_uinteger_t *);
static void							wd_account_read_privileges_from_message(wd_account_t *, wi_p7_message_t *);
static void							wd_account_write_privileges_to_message(wd_account_t *, wi_p7_message_t *);
static wi_array_t *					wd_account_user_array(wd_account_t *);
static wi_array_t *					wd_account_group_array(wd_account_t *);

static wi_string_t					*wd_users_path;
static wi_string_t					*wd_groups_path;

static wi_recursive_lock_t			*wd_users_lock;
static wi_recursive_lock_t			*wd_groups_lock;

static wi_runtime_id_t				wd_account_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t			wd_account_runtime_class = {
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
}



#pragma mark -

wd_account_t * wd_accounts_read_user_and_group(wi_string_t *name) {
	wd_account_t		*user, *group;
	
	user = wd_accounts_read_user(name);
	
	if(!user)
		return NULL;
	
	if(wi_string_length(user->group) > 0) {
		group = wd_accounts_read_group(user->group);
		
		if(group)
			wd_accounts_copy_attributes(group, user);
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
				account = wd_account_init_user_with_array(wd_account_alloc(), array);
				
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
				account = wd_account_init_group_with_array(wd_account_alloc(), array);
				
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
		
	if(account) {
		if(wi_string_length(account->password) == 0)
			return wi_string_sha1(WI_STR(""));
		
		return account->password;
	}
	
	return NULL;
}



wi_boolean_t wd_accounts_change_password(wd_account_t *account, wi_string_t *password) {
	wi_boolean_t		result = false;
	
	wi_release(account->password);
	account->password = wi_retain(password);
	
	wi_recursive_lock_lock(wd_users_lock);
	
	if(wd_accounts_delete_user(account->name)) {
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
		array = wd_account_user_array(account);

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
		array = wd_account_group_array(account);

		wi_file_write_format(file, WI_STR("%@\n"), wi_array_components_joined_by_string(array, WI_STR(":")));
		
		result = true;
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_groups_path);
	}

	wi_recursive_lock_unlock(wd_groups_lock);
	
	return result;
}



wi_boolean_t wd_accounts_edit_user(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_uinteger_t	i, count;
	wi_boolean_t	result = false;
	
	wi_release(account->password);
	account->password = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.password")));
	wi_string_replace_string_with_string(account->password, WI_STR(":"), WI_STR(""), 0);
	
	wi_release(account->full_name);
	account->full_name = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.full_name")));
	wi_string_replace_string_with_string(account->full_name, WI_STR(":"), WI_STR(""), 0);
	
	wi_release(account->group);
	account->group = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.group")));
	wi_string_replace_string_with_string(account->group, WI_STR(":"), WI_STR(""), 0);
	
	wi_release(account->files);
	account->files = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.files")));
	wi_string_replace_string_with_string(account->files, WI_STR(":"), WI_STR(""), 0);
	
	wi_release(account->groups);
	account->groups = wi_retain(wi_p7_message_list_for_name(message, WI_STR("wired.account.groups")));
	
	count = wi_array_count(account->groups);
	
	for(i = 0; i < count; i++)
		wi_string_replace_string_with_string(WI_ARRAY(account->groups, i), WI_STR(":"), WI_STR(""), 0);
	
	wi_release(account->modification_time);
	account->modification_time = wi_retain(wi_date());
	
	wi_release(account->edited_by);
	account->edited_by = wi_retain(wd_user_nick(user));

	wd_account_read_privileges_from_message(account, message);
	
	wi_recursive_lock_lock(wd_users_lock);
	
	if(wd_accounts_delete_user(account->name)) {
		if(wd_accounts_create_user(account))
			result = true;
	}
	
	wi_recursive_lock_unlock(wd_users_lock);
	
	return result;
}



wi_boolean_t wd_accounts_edit_group(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_boolean_t	result = false;
	
	wi_release(account->files);
	account->files = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.files")));
	wi_string_replace_string_with_string(account->files, WI_STR(":"), WI_STR(""), 0);
	
	wi_release(account->modification_time);
	account->modification_time = wi_retain(wi_date());
	
	wi_release(account->edited_by);
	account->edited_by = wi_retain(wd_user_nick(user));
	
	wd_account_read_privileges_from_message(account, message);
	
	wi_recursive_lock_lock(wd_groups_lock);
	
	if(wd_accounts_delete_group(account->name)) {
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
	wi_release(account->login_time);
	account->login_time = wi_retain(wi_date());
	
	wi_recursive_lock_lock(wd_users_lock);
	
	if(wd_accounts_delete_user(account->name))
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
		
		if(account && wi_is_equal(account->name, name))
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
		
		if(account && wi_is_equal(account->group, name))
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
	
	new_account = wd_accounts_read_user_and_group(account->name);
	
	if(!new_account)
		return;
	
	wd_user_set_account(user, new_account);
	
	admin = wd_user_is_admin(user);
	new_admin = (new_account->user_kick_users || new_account->user_ban_users);
	wd_user_set_admin(user, new_admin);
	
	if(admin != new_admin)
		wd_user_broadcast_status(user);
	
	if(!new_account->log_view_log && wd_user_is_subscribed_log(user, NULL))
		wd_user_unsubscribe_log(user);
	
	if(!new_account->file_list_files && wi_set_count(wd_user_subscribed_paths(user)) > 0)
		wd_user_unsubscribe_paths(user);
	
	wd_user_send_message(user, wd_account_privileges_message(new_account));
}



static void wd_accounts_copy_attributes(wd_account_t *src_account, wd_account_t *dst_account) {
	size_t			size, offset;
	
	size = sizeof(wd_account_t);
	offset = offsetof(wd_account_t, user_cannot_set_nick);
	
	memcpy(dst_account + offset, src_account + offset, size - offset);
	
	wi_release(dst_account->files);
	dst_account->files = wi_retain(src_account->files);
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



wd_account_t * wd_account_init_with_message(wd_account_t *account, wi_p7_message_t *message) {
	wi_uinteger_t		i, count;
	
	account->name = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.name")));
	wi_string_replace_string_with_string(account->name, WI_STR(":"), WI_STR(""), 0);
	wi_string_replace_string_with_string(account->name, WI_STR(","), WI_STR(""), 0);

	account->password = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.password")));
	
	if(account->password)
		wi_string_replace_string_with_string(account->password, WI_STR(":"), WI_STR(""), 0);

	account->full_name = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.full_name")));
	
	if(account->full_name)
		wi_string_replace_string_with_string(account->full_name, WI_STR(":"), WI_STR(""), 0);

	account->group = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.group")));
	
	if(account->group)
		wi_string_replace_string_with_string(account->group, WI_STR(":"), WI_STR(""), 0);

	account->files = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.account.files")));
	wi_string_replace_string_with_string(account->files, WI_STR(":"), WI_STR(""), 0);

	account->groups = wi_retain(wi_p7_message_list_for_name(message, WI_STR("wired.account.groups")));
	
	if(account->groups) {
		count = wi_array_count(account->groups);

		for(i = 0; i < count; i++)
			wi_string_replace_string_with_string(WI_ARRAY(account->groups, i), WI_STR(":"), WI_STR(""), 0);
	}
	
	wd_account_read_privileges_from_message(account, message);
	
	return account;
}



wd_account_t * wd_account_init_user_with_array(wd_account_t *account, wi_array_t *array) {
	wi_uinteger_t		i, count;
	
	i = 0;
	count = wi_array_count(array);

	account->name								= wi_retain(wd_account_next_string(array, &i));
	account->password							= wi_retain(wd_account_next_string(array, &i));
	account->group								= wi_retain(wd_account_next_string(array, &i));
	account->user_get_info						= wd_account_next_bool(array, true, &i);
	account->message_broadcast					= wd_account_next_bool(array, false, &i);
	account->board_add_posts					= wd_account_next_bool(array, true, &i);
	account->board_delete_posts					= wd_account_next_bool(array, true, &i);
	account->transfer_download_files			= wd_account_next_bool(array, true, &i);
	account->transfer_upload_files				= wd_account_next_bool(array, true, &i);
	account->transfer_upload_anywhere			= wd_account_next_bool(array, false, &i);
	account->file_create_directories			= wd_account_next_bool(array, false, &i);
	account->file_move_files					= wd_account_next_bool(array, false, &i);
	account->file_delete_files					= wd_account_next_bool(array, false, &i);
	account->file_access_all_dropboxes			= wd_account_next_bool(array, false, &i);
	account->account_create_accounts			= wd_account_next_bool(array, false, &i);
	account->account_edit_accounts				= wd_account_next_bool(array, false, &i);
	account->account_delete_accounts			= wd_account_next_bool(array, false, &i);
	account->account_raise_account_privileges	= wd_account_next_bool(array, false, &i);
	account->user_kick_users					= wd_account_next_bool(array, false, &i);
	account->user_ban_users						= wd_account_next_bool(array, false, &i);
	account->user_cannot_be_disconnected		= wd_account_next_bool(array, false, &i);
	account->transfer_download_speed_limit		= wd_account_next_uinteger(array, &i);
	account->transfer_upload_speed_limit		= wd_account_next_uinteger(array, &i);
	account->transfer_download_limit			= wd_account_next_uinteger(array, &i);
	account->transfer_upload_limit				= wd_account_next_uinteger(array, &i);
	account->chat_set_topic						= wd_account_next_bool(array, false, &i);
	account->files								= wi_retain(wd_account_next_string(array, &i));
	account->full_name							= wi_retain(wd_account_next_string(array, &i));
	account->creation_time						= wi_retain(wd_account_next_date(array, &i));
	account->modification_time					= wi_retain(wd_account_next_date(array, &i));
	account->login_time							= wi_retain(wd_account_next_date(array, &i));
	account->edited_by							= wi_retain(wd_account_next_string(array, &i));
	account->groups								= wi_retain(wd_account_next_array(array, &i));
	account->chat_create_chats					= wd_account_next_bool(array, true, &i);
	account->message_send_messages				= wd_account_next_bool(array, true, &i);
	account->board_read_boards					= wd_account_next_bool(array, true, &i);
	account->file_list_files					= wd_account_next_bool(array, true, &i);
	account->file_get_info						= wd_account_next_bool(array, true, &i);
	account->file_create_links					= wd_account_next_bool(array, false, &i);
	account->file_rename_files					= wd_account_next_bool(array, false, &i);
	account->file_set_type						= wd_account_next_bool(array, false, &i);
	account->file_set_comment					= wd_account_next_bool(array, false, &i);
	account->file_set_permissions				= wd_account_next_bool(array, false, &i);
	account->file_set_executable				= wd_account_next_bool(array, false, &i);
	account->file_recursive_list_depth_limit	= wd_account_next_uinteger(array, &i);
	account->transfer_upload_directories		= wd_account_next_bool(array, false, &i);
	account->account_change_password			= wd_account_next_bool(array, false, &i);
	account->account_list_accounts				= wd_account_next_bool(array, false, &i);
	account->account_read_accounts				= wd_account_next_bool(array, false, &i);
	account->user_cannot_set_nick				= wd_account_next_bool(array, false, &i);
	account->user_get_users						= wd_account_next_bool(array, false, &i);
	account->log_view_log						= wd_account_next_bool(array, false, &i);
	account->settings_get_settings				= wd_account_next_bool(array, false, &i);
	account->settings_set_settings				= wd_account_next_bool(array, false, &i);
	account->banlist_get_bans					= wd_account_next_bool(array, false, &i);
	account->banlist_add_bans					= wd_account_next_bool(array, false, &i);
	account->banlist_delete_bans				= wd_account_next_bool(array, false, &i);
	account->tracker_list_servers				= wd_account_next_bool(array, false, &i);
	account->tracker_register_servers			= wd_account_next_bool(array, false, &i);
	account->board_add_boards					= wd_account_next_bool(array, false, &i);
	account->board_move_boards					= wd_account_next_bool(array, false, &i);
	account->board_rename_boards				= wd_account_next_bool(array, false, &i);
	account->board_delete_boards				= wd_account_next_bool(array, false, &i);
	account->board_add_threads					= wd_account_next_bool(array, true, &i);
	account->board_move_threads					= wd_account_next_bool(array, false, &i);
	account->board_delete_threads				= wd_account_next_bool(array, false, &i);
	account->board_edit_own_posts				= wd_account_next_bool(array, true, &i);
	account->board_edit_all_posts				= wd_account_next_bool(array, false, &i);

	if(wi_string_length(account->password) == 0) {
		wi_release(account->password);
		account->password = wi_retain(wi_string_sha1(account->password));
	}
	
	return account;
}



wd_account_t * wd_account_init_group_with_array(wd_account_t *account, wi_array_t *array) {
	wi_uinteger_t	i, count;
	
	i = 0;
	count = wi_array_count(array);

	account->name								= wi_retain(wd_account_next_string(array, &i));
	account->user_get_info						= wd_account_next_bool(array, false, &i);
	account->message_broadcast					= wd_account_next_bool(array, false, &i);
	account->board_add_posts					= wd_account_next_bool(array, false, &i);
	account->board_delete_posts					= wd_account_next_bool(array, false, &i);
	account->transfer_download_files			= wd_account_next_bool(array, false, &i);
	account->transfer_upload_files				= wd_account_next_bool(array, false, &i);
	account->transfer_upload_anywhere			= wd_account_next_bool(array, false, &i);
	account->file_create_directories			= wd_account_next_bool(array, false, &i);
	account->file_move_files					= wd_account_next_bool(array, false, &i);
	account->file_delete_files					= wd_account_next_bool(array, false, &i);
	account->file_access_all_dropboxes			= wd_account_next_bool(array, false, &i);
	account->account_create_accounts			= wd_account_next_bool(array, false, &i);
	account->account_edit_accounts				= wd_account_next_bool(array, false, &i);
	account->account_delete_accounts			= wd_account_next_bool(array, false, &i);
	account->account_raise_account_privileges	= wd_account_next_bool(array, false, &i);
	account->user_kick_users					= wd_account_next_bool(array, false, &i);
	account->user_ban_users						= wd_account_next_bool(array, false, &i);
	account->user_cannot_be_disconnected		= wd_account_next_bool(array, false, &i);
	account->transfer_download_speed_limit		= wd_account_next_uinteger(array, &i);
	account->transfer_upload_speed_limit		= wd_account_next_uinteger(array, &i);
	account->transfer_download_limit			= wd_account_next_uinteger(array, &i);
	account->transfer_upload_limit				= wd_account_next_uinteger(array, &i);
	account->chat_set_topic						= wd_account_next_bool(array, false, &i);
	account->files								= wi_retain(wd_account_next_string(array, &i));
	account->creation_time						= wi_retain(wd_account_next_date(array, &i));
	account->modification_time					= wi_retain(wd_account_next_date(array, &i));
	account->edited_by							= wi_retain(wd_account_next_string(array, &i));
	account->chat_create_chats					= wd_account_next_bool(array, true, &i);
	account->message_send_messages				= wd_account_next_bool(array, true, &i);
	account->board_read_boards					= wd_account_next_bool(array, true, &i);
	account->file_list_files					= wd_account_next_bool(array, true, &i);
	account->file_get_info						= wd_account_next_bool(array, true, &i);
	account->file_create_links					= wd_account_next_bool(array, false, &i);
	account->file_rename_files					= wd_account_next_bool(array, false, &i);
	account->file_set_type						= wd_account_next_bool(array, false, &i);
	account->file_set_comment					= wd_account_next_bool(array, false, &i);
	account->file_set_permissions				= wd_account_next_bool(array, false, &i);
	account->file_set_executable				= wd_account_next_bool(array, false, &i);
	account->file_recursive_list_depth_limit	= wd_account_next_uinteger(array, &i);
	account->transfer_upload_directories		= wd_account_next_bool(array, false, &i);
	account->account_change_password			= wd_account_next_bool(array, false, &i);
	account->account_list_accounts				= wd_account_next_bool(array, false, &i);
	account->account_read_accounts				= wd_account_next_bool(array, false, &i);
	account->user_cannot_set_nick				= wd_account_next_bool(array, false, &i);
	account->user_get_users						= wd_account_next_bool(array, false, &i);
	account->log_view_log						= wd_account_next_bool(array, false, &i);
	account->settings_get_settings				= wd_account_next_bool(array, false, &i);
	account->settings_set_settings				= wd_account_next_bool(array, false, &i);
	account->banlist_get_bans					= wd_account_next_bool(array, false, &i);
	account->banlist_add_bans					= wd_account_next_bool(array, false, &i);
	account->banlist_delete_bans				= wd_account_next_bool(array, false, &i);
	account->tracker_list_servers				= wd_account_next_bool(array, false, &i);
	account->tracker_register_servers			= wd_account_next_bool(array, false, &i);
	account->board_add_boards					= wd_account_next_bool(array, false, &i);
	account->board_move_boards					= wd_account_next_bool(array, false, &i);
	account->board_rename_boards				= wd_account_next_bool(array, false, &i);
	account->board_delete_boards				= wd_account_next_bool(array, false, &i);
	account->board_add_threads					= wd_account_next_bool(array, true, &i);
	account->board_move_threads					= wd_account_next_bool(array, false, &i);
	account->board_delete_threads				= wd_account_next_bool(array, false, &i);
	account->board_edit_own_posts				= wd_account_next_bool(array, true, &i);
	account->board_edit_all_posts				= wd_account_next_bool(array, false, &i);
	
	return account;
}



static void wd_account_dealloc(wi_runtime_instance_t *instance) {
	wd_account_t		*account = instance;
	
	wi_release(account->name);
	wi_release(account->full_name);
	wi_release(account->creation_time);
	wi_release(account->modification_time);
	wi_release(account->login_time);
	wi_release(account->edited_by);
	wi_release(account->password);
	wi_release(account->group);
	wi_release(account->groups);
	wi_release(account->files);
}



#pragma mark -

static wi_string_t * wd_account_next_string(wi_array_t *array, wi_uinteger_t *index) {
	wi_string_t		*string;
	
	if(wi_array_count(array) > *index)
		string = WI_ARRAY(array, *index);
	else
		string = WI_STR("");
	
	(*index)++;
	
	return string;
}



static wi_array_t * wd_account_next_array(wi_array_t *array, wi_uinteger_t *index) {
	return wi_string_components_separated_by_string(wd_account_next_string(array, index), WI_STR(","));
}



static wi_date_t * wd_account_next_date(wi_array_t *array, wi_uinteger_t *index) {
	wi_string_t		*string;
	wi_date_t		*date;
	
	string = wi_string_by_replacing_string_with_string(wd_account_next_string(array, index), WI_STR(";"), WI_STR(":"), 0);
	date = wi_date_with_rfc3339_string(string);
	
	if(!date)
		date = wi_date_with_time(0);
	
	return date;
}



static wi_boolean_t wd_account_next_bool(wi_array_t *array, wi_boolean_t defaultvalue, wi_uinteger_t *index) {
	wi_string_t		*string;
	
	string = wd_account_next_string(array, index);
	
	if(wi_string_length(string) == 0)
		return defaultvalue;
	
	return wi_string_bool(string);
}



static wi_uinteger_t wd_account_next_uinteger(wi_array_t *array, wi_uinteger_t *index) {
	return wi_string_uinteger(wd_account_next_string(array, index));
}



#pragma mark -

#define WD_ACCOUNT_GET_BOOL(value, name) \
	wi_p7_message_get_bool_for_name(message, (value), (name))

#define WD_ACCOUNT_GET_UINT32(value, name) \
	wi_p7_message_get_uint32_for_name(message, (value), (name))

static void wd_account_read_privileges_from_message(wd_account_t *account, wi_p7_message_t *message) {
	WD_ACCOUNT_GET_BOOL(&account->user_cannot_set_nick, WI_STR("wired.account.user.cannot_set_nick"));
	WD_ACCOUNT_GET_BOOL(&account->user_get_info, WI_STR("wired.account.user.get_info"));
	WD_ACCOUNT_GET_BOOL(&account->user_kick_users, WI_STR("wired.account.user.kick_users"));
	WD_ACCOUNT_GET_BOOL(&account->user_ban_users, WI_STR("wired.account.user.ban_users"));
	WD_ACCOUNT_GET_BOOL(&account->user_cannot_be_disconnected, WI_STR("wired.account.user.cannot_be_disconnected"));
	WD_ACCOUNT_GET_BOOL(&account->user_get_users, WI_STR("wired.account.user.get_users"));

	WD_ACCOUNT_GET_BOOL(&account->chat_set_topic, WI_STR("wired.account.chat.set_topic"));
	WD_ACCOUNT_GET_BOOL(&account->chat_create_chats, WI_STR("wired.account.chat.create_chats"));

	WD_ACCOUNT_GET_BOOL(&account->message_send_messages, WI_STR("wired.account.message.send_messages"));
	WD_ACCOUNT_GET_BOOL(&account->message_broadcast, WI_STR("wired.account.message.broadcast"));

	WD_ACCOUNT_GET_BOOL(&account->board_read_boards, WI_STR("wired.account.board.read_boards"));
	WD_ACCOUNT_GET_BOOL(&account->board_add_boards, WI_STR("wired.account.board.add_boards"));
	WD_ACCOUNT_GET_BOOL(&account->board_move_boards, WI_STR("wired.account.board.move_boards"));
	WD_ACCOUNT_GET_BOOL(&account->board_rename_boards, WI_STR("wired.account.board.rename_boards"));
	WD_ACCOUNT_GET_BOOL(&account->board_delete_boards, WI_STR("wired.account.board.delete_boards"));
	WD_ACCOUNT_GET_BOOL(&account->board_add_threads, WI_STR("wired.account.board.add_threads"));
	WD_ACCOUNT_GET_BOOL(&account->board_move_threads, WI_STR("wired.account.board.move_threads"));
	WD_ACCOUNT_GET_BOOL(&account->board_delete_threads, WI_STR("wired.account.board.delete_threads"));
	WD_ACCOUNT_GET_BOOL(&account->board_add_posts, WI_STR("wired.account.board.add_posts"));
	WD_ACCOUNT_GET_BOOL(&account->board_edit_own_posts, WI_STR("wired.account.board.edit_own_posts"));
	WD_ACCOUNT_GET_BOOL(&account->board_edit_all_posts, WI_STR("wired.account.board.edit_all_posts"));
	WD_ACCOUNT_GET_BOOL(&account->board_delete_posts, WI_STR("wired.account.board.delete_posts"));
	
	WD_ACCOUNT_GET_BOOL(&account->file_list_files, WI_STR("wired.account.file.list_files"));
	WD_ACCOUNT_GET_BOOL(&account->file_get_info, WI_STR("wired.account.file.get_info"));
	WD_ACCOUNT_GET_BOOL(&account->file_create_directories, WI_STR("wired.account.file.create_directories"));
	WD_ACCOUNT_GET_BOOL(&account->file_create_links, WI_STR("wired.account.file.create_links"));
	WD_ACCOUNT_GET_BOOL(&account->file_move_files, WI_STR("wired.account.file.move_files"));
	WD_ACCOUNT_GET_BOOL(&account->file_rename_files, WI_STR("wired.account.file.rename_files"));
	WD_ACCOUNT_GET_BOOL(&account->file_set_type, WI_STR("wired.account.file.set_type"));
	WD_ACCOUNT_GET_BOOL(&account->file_set_comment, WI_STR("wired.account.file.set_comment"));
	WD_ACCOUNT_GET_BOOL(&account->file_set_permissions, WI_STR("wired.account.file.set_permissions"));
	WD_ACCOUNT_GET_BOOL(&account->file_set_executable, WI_STR("wired.account.file.set_executable"));
	WD_ACCOUNT_GET_BOOL(&account->file_delete_files, WI_STR("wired.account.file.delete_files"));
	WD_ACCOUNT_GET_BOOL(&account->file_access_all_dropboxes, WI_STR("wired.account.file.access_all_dropboxes"));
	WD_ACCOUNT_GET_UINT32(&account->file_recursive_list_depth_limit, WI_STR("wired.account.file.recursive_list_depth_limit"));

	WD_ACCOUNT_GET_BOOL(&account->transfer_download_files, WI_STR("wired.account.transfer.download_files"));
	WD_ACCOUNT_GET_BOOL(&account->transfer_upload_files, WI_STR("wired.account.transfer.upload_files"));
	WD_ACCOUNT_GET_BOOL(&account->transfer_upload_directories, WI_STR("wired.account.transfer.upload_directories"));
	WD_ACCOUNT_GET_BOOL(&account->transfer_upload_anywhere, WI_STR("wired.account.transfer.upload_anywhere"));
	WD_ACCOUNT_GET_UINT32(&account->transfer_download_limit, WI_STR("wired.account.transfer.download_limit"));
	WD_ACCOUNT_GET_UINT32(&account->transfer_upload_limit, WI_STR("wired.account.transfer.upload_limit"));
	WD_ACCOUNT_GET_UINT32(&account->transfer_download_speed_limit, WI_STR("wired.account.transfer.download_speed_limit"));
	WD_ACCOUNT_GET_UINT32(&account->transfer_upload_speed_limit, WI_STR("wired.account.transfer.upload_speed_limit"));

	WD_ACCOUNT_GET_BOOL(&account->account_change_password, WI_STR("wired.account.account.change_password"));
	WD_ACCOUNT_GET_BOOL(&account->account_list_accounts, WI_STR("wired.account.account.list_accounts"));
	WD_ACCOUNT_GET_BOOL(&account->account_read_accounts, WI_STR("wired.account.account.read_accounts"));
	WD_ACCOUNT_GET_BOOL(&account->account_create_accounts, WI_STR("wired.account.account.create_accounts"));
	WD_ACCOUNT_GET_BOOL(&account->account_edit_accounts, WI_STR("wired.account.account.edit_accounts"));
	WD_ACCOUNT_GET_BOOL(&account->account_delete_accounts, WI_STR("wired.account.account.delete_accounts"));
	WD_ACCOUNT_GET_BOOL(&account->account_raise_account_privileges, WI_STR("wired.account.account.raise_account_privileges"));

	WD_ACCOUNT_GET_BOOL(&account->log_view_log, WI_STR("wired.account.log.view_log"));
	WD_ACCOUNT_GET_BOOL(&account->settings_get_settings, WI_STR("wired.account.settings.get_settings"));
	WD_ACCOUNT_GET_BOOL(&account->settings_set_settings, WI_STR("wired.account.settings.set_settings"));
	WD_ACCOUNT_GET_BOOL(&account->banlist_get_bans, WI_STR("wired.account.banlist.get_bans"));
	WD_ACCOUNT_GET_BOOL(&account->banlist_add_bans, WI_STR("wired.account.banlist.add_bans"));
	WD_ACCOUNT_GET_BOOL(&account->banlist_delete_bans, WI_STR("wired.account.banlist.delete_bans"));
	WD_ACCOUNT_GET_BOOL(&account->tracker_list_servers, WI_STR("wired.account.tracker.list_servers"));
	WD_ACCOUNT_GET_BOOL(&account->tracker_register_servers, WI_STR("wired.account.tracker.register_servers"));
}



#define WD_ACCOUNT_SET_BOOL(value, name) \
	wi_p7_message_set_bool_for_name(message, (value), (name))

#define WD_ACCOUNT_SET_UINT32(value, name) \
	wi_p7_message_set_uint32_for_name(message, (value), (name))

static void wd_account_write_privileges_to_message(wd_account_t *account, wi_p7_message_t *message) {
	WD_ACCOUNT_SET_BOOL(account->user_cannot_set_nick, WI_STR("wired.account.user.cannot_set_nick"));
	WD_ACCOUNT_SET_BOOL(account->user_cannot_set_nick, WI_STR("wired.account.user.cannot_set_nick"));
	WD_ACCOUNT_SET_BOOL(account->user_get_info, WI_STR("wired.account.user.get_info"));
	WD_ACCOUNT_SET_BOOL(account->user_kick_users, WI_STR("wired.account.user.kick_users"));
	WD_ACCOUNT_SET_BOOL(account->user_ban_users, WI_STR("wired.account.user.ban_users"));
	WD_ACCOUNT_SET_BOOL(account->user_cannot_be_disconnected, WI_STR("wired.account.user.cannot_be_disconnected"));
	WD_ACCOUNT_SET_BOOL(account->user_get_users, WI_STR("wired.account.user.get_users"));

	WD_ACCOUNT_SET_BOOL(account->chat_set_topic, WI_STR("wired.account.chat.set_topic"));
	WD_ACCOUNT_SET_BOOL(account->chat_create_chats, WI_STR("wired.account.chat.create_chats"));

	WD_ACCOUNT_SET_BOOL(account->message_send_messages, WI_STR("wired.account.message.send_messages"));
	WD_ACCOUNT_SET_BOOL(account->message_broadcast, WI_STR("wired.account.message.broadcast"));

	WD_ACCOUNT_SET_BOOL(account->board_read_boards, WI_STR("wired.account.board.read_boards"));
	WD_ACCOUNT_SET_BOOL(account->board_add_boards, WI_STR("wired.account.board.add_boards"));
	WD_ACCOUNT_SET_BOOL(account->board_move_boards, WI_STR("wired.account.board.move_boards"));
	WD_ACCOUNT_SET_BOOL(account->board_rename_boards, WI_STR("wired.account.board.rename_boards"));
	WD_ACCOUNT_SET_BOOL(account->board_delete_boards, WI_STR("wired.account.board.delete_boards"));
	WD_ACCOUNT_SET_BOOL(account->board_add_threads, WI_STR("wired.account.board.add_threads"));
	WD_ACCOUNT_SET_BOOL(account->board_move_threads, WI_STR("wired.account.board.move_threads"));
	WD_ACCOUNT_SET_BOOL(account->board_delete_threads, WI_STR("wired.account.board.delete_threads"));
	WD_ACCOUNT_SET_BOOL(account->board_add_posts, WI_STR("wired.account.board.add_posts"));
	WD_ACCOUNT_SET_BOOL(account->board_edit_own_posts, WI_STR("wired.account.board.edit_own_posts"));
	WD_ACCOUNT_SET_BOOL(account->board_edit_all_posts, WI_STR("wired.account.board.edit_all_posts"));
	WD_ACCOUNT_SET_BOOL(account->board_delete_posts, WI_STR("wired.account.board.delete_posts"));
	
	WD_ACCOUNT_SET_BOOL(account->file_list_files, WI_STR("wired.account.file.list_files"));
	WD_ACCOUNT_SET_BOOL(account->file_get_info, WI_STR("wired.account.file.get_info"));
	WD_ACCOUNT_SET_BOOL(account->file_create_directories, WI_STR("wired.account.file.create_directories"));
	WD_ACCOUNT_SET_BOOL(account->file_create_links, WI_STR("wired.account.file.create_links"));
	WD_ACCOUNT_SET_BOOL(account->file_move_files, WI_STR("wired.account.file.move_files"));
	WD_ACCOUNT_SET_BOOL(account->file_rename_files, WI_STR("wired.account.file.rename_files"));
	WD_ACCOUNT_SET_BOOL(account->file_set_type, WI_STR("wired.account.file.set_type"));
	WD_ACCOUNT_SET_BOOL(account->file_set_comment, WI_STR("wired.account.file.set_comment"));
	WD_ACCOUNT_SET_BOOL(account->file_set_permissions, WI_STR("wired.account.file.set_permissions"));
	WD_ACCOUNT_SET_BOOL(account->file_set_executable, WI_STR("wired.account.file.set_executable"));
	WD_ACCOUNT_SET_BOOL(account->file_delete_files, WI_STR("wired.account.file.delete_files"));
	WD_ACCOUNT_SET_BOOL(account->file_access_all_dropboxes, WI_STR("wired.account.file.access_all_dropboxes"));
	WD_ACCOUNT_SET_UINT32(account->file_recursive_list_depth_limit, WI_STR("wired.account.file.recursive_list_depth_limit"));

	WD_ACCOUNT_SET_BOOL(account->transfer_download_files, WI_STR("wired.account.transfer.download_files"));
	WD_ACCOUNT_SET_BOOL(account->transfer_upload_files, WI_STR("wired.account.transfer.upload_files"));
	WD_ACCOUNT_SET_BOOL(account->transfer_upload_directories, WI_STR("wired.account.transfer.upload_directories"));
	WD_ACCOUNT_SET_BOOL(account->transfer_upload_anywhere, WI_STR("wired.account.transfer.upload_anywhere"));
	WD_ACCOUNT_SET_UINT32(account->transfer_download_limit, WI_STR("wired.account.transfer.download_limit"));
	WD_ACCOUNT_SET_UINT32(account->transfer_upload_limit, WI_STR("wired.account.transfer.upload_limit"));
	WD_ACCOUNT_SET_UINT32(account->transfer_download_speed_limit, WI_STR("wired.account.transfer.download_speed_limit"));
	WD_ACCOUNT_SET_UINT32(account->transfer_upload_speed_limit, WI_STR("wired.account.transfer.upload_speed_limit"));

	WD_ACCOUNT_SET_BOOL(account->account_change_password, WI_STR("wired.account.account.change_password"));
	WD_ACCOUNT_SET_BOOL(account->account_list_accounts, WI_STR("wired.account.account.list_accounts"));
	WD_ACCOUNT_SET_BOOL(account->account_read_accounts, WI_STR("wired.account.account.read_accounts"));
	WD_ACCOUNT_SET_BOOL(account->account_create_accounts, WI_STR("wired.account.account.create_accounts"));
	WD_ACCOUNT_SET_BOOL(account->account_edit_accounts, WI_STR("wired.account.account.edit_accounts"));
	WD_ACCOUNT_SET_BOOL(account->account_delete_accounts, WI_STR("wired.account.account.delete_accounts"));
	WD_ACCOUNT_SET_BOOL(account->account_raise_account_privileges, WI_STR("wired.account.account.raise_account_privileges"));

	WD_ACCOUNT_SET_BOOL(account->log_view_log, WI_STR("wired.account.log.view_log"));
	WD_ACCOUNT_SET_BOOL(account->settings_get_settings, WI_STR("wired.account.settings.get_settings"));
	WD_ACCOUNT_SET_BOOL(account->settings_set_settings, WI_STR("wired.account.settings.set_settings"));
	WD_ACCOUNT_SET_BOOL(account->banlist_get_bans, WI_STR("wired.account.banlist.get_bans"));
	WD_ACCOUNT_SET_BOOL(account->banlist_add_bans, WI_STR("wired.account.banlist.add_bans"));
	WD_ACCOUNT_SET_BOOL(account->banlist_delete_bans, WI_STR("wired.account.banlist.delete_bans"));
	WD_ACCOUNT_SET_BOOL(account->tracker_list_servers, WI_STR("wired.account.tracker.list_servers"));
	WD_ACCOUNT_SET_BOOL(account->tracker_register_servers, WI_STR("wired.account.tracker.register_servers"));
}



#define WD_ACCOUNT_ADD_BOOL(value) \
	wi_array_add_data(array, wi_string_with_format(WI_STR("%u"), (value)))

static wi_array_t * wd_account_user_array(wd_account_t *account) {
	wi_array_t		*array;
	
	array = wi_array();
	wi_array_add_data(array, account->name);
	wi_array_add_data(array, account->password);
	wi_array_add_data(array, account->group);
	WD_ACCOUNT_ADD_BOOL(account->user_get_info);
	WD_ACCOUNT_ADD_BOOL(account->message_broadcast);
	WD_ACCOUNT_ADD_BOOL(account->board_add_posts);
	WD_ACCOUNT_ADD_BOOL(account->board_delete_posts);
	WD_ACCOUNT_ADD_BOOL(account->transfer_download_files);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_files);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_anywhere);
	WD_ACCOUNT_ADD_BOOL(account->file_create_directories);
	WD_ACCOUNT_ADD_BOOL(account->file_move_files);
	WD_ACCOUNT_ADD_BOOL(account->file_delete_files);
	WD_ACCOUNT_ADD_BOOL(account->file_access_all_dropboxes);
	WD_ACCOUNT_ADD_BOOL(account->account_create_accounts);
	WD_ACCOUNT_ADD_BOOL(account->account_edit_accounts);
	WD_ACCOUNT_ADD_BOOL(account->account_delete_accounts);
	WD_ACCOUNT_ADD_BOOL(account->account_raise_account_privileges);
	WD_ACCOUNT_ADD_BOOL(account->user_kick_users);
	WD_ACCOUNT_ADD_BOOL(account->user_ban_users);
	WD_ACCOUNT_ADD_BOOL(account->user_cannot_be_disconnected);
	WD_ACCOUNT_ADD_BOOL(account->transfer_download_speed_limit);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_speed_limit);
	WD_ACCOUNT_ADD_BOOL(account->transfer_download_limit);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_limit);
	WD_ACCOUNT_ADD_BOOL(account->chat_set_topic);
	wi_array_add_data(array, account->files);
	wi_array_add_data(array, account->full_name);
	wi_array_add_data(array, wi_string_by_replacing_string_with_string(wi_date_rfc3339_string(account->creation_time),
		WI_STR(":"), WI_STR(";"), 0));
	wi_array_add_data(array, wi_string_by_replacing_string_with_string(wi_date_rfc3339_string(account->modification_time),
		WI_STR(":"), WI_STR(";"), 0));
	wi_array_add_data(array, wi_string_by_replacing_string_with_string(wi_date_rfc3339_string(account->login_time),
		WI_STR(":"), WI_STR(";"), 0));
	wi_array_add_data(array, account->edited_by);
	wi_array_add_data(array, wi_array_components_joined_by_string(account->groups, WI_STR(",")));
	WD_ACCOUNT_ADD_BOOL(account->chat_create_chats);
	WD_ACCOUNT_ADD_BOOL(account->message_send_messages);
	WD_ACCOUNT_ADD_BOOL(account->board_read_boards);
	WD_ACCOUNT_ADD_BOOL(account->file_list_files);
	WD_ACCOUNT_ADD_BOOL(account->file_get_info);
	WD_ACCOUNT_ADD_BOOL(account->file_create_links);
	WD_ACCOUNT_ADD_BOOL(account->file_rename_files);
	WD_ACCOUNT_ADD_BOOL(account->file_set_type);
	WD_ACCOUNT_ADD_BOOL(account->file_set_comment);
	WD_ACCOUNT_ADD_BOOL(account->file_set_permissions);
	WD_ACCOUNT_ADD_BOOL(account->file_set_executable);
	WD_ACCOUNT_ADD_BOOL(account->file_recursive_list_depth_limit);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_directories);
	WD_ACCOUNT_ADD_BOOL(account->account_change_password);
	WD_ACCOUNT_ADD_BOOL(account->account_list_accounts);
	WD_ACCOUNT_ADD_BOOL(account->account_read_accounts);
	WD_ACCOUNT_ADD_BOOL(account->user_cannot_set_nick);
	WD_ACCOUNT_ADD_BOOL(account->user_get_users);
	WD_ACCOUNT_ADD_BOOL(account->log_view_log);
	WD_ACCOUNT_ADD_BOOL(account->settings_get_settings);
	WD_ACCOUNT_ADD_BOOL(account->settings_set_settings);
	WD_ACCOUNT_ADD_BOOL(account->banlist_get_bans);
	WD_ACCOUNT_ADD_BOOL(account->banlist_add_bans);
	WD_ACCOUNT_ADD_BOOL(account->banlist_delete_bans);
	WD_ACCOUNT_ADD_BOOL(account->tracker_list_servers);
	WD_ACCOUNT_ADD_BOOL(account->tracker_register_servers);
	WD_ACCOUNT_ADD_BOOL(account->board_add_boards);
	WD_ACCOUNT_ADD_BOOL(account->board_move_boards);
	WD_ACCOUNT_ADD_BOOL(account->board_rename_boards);
	WD_ACCOUNT_ADD_BOOL(account->board_delete_boards);
	WD_ACCOUNT_ADD_BOOL(account->board_add_threads);
	WD_ACCOUNT_ADD_BOOL(account->board_move_threads);
	WD_ACCOUNT_ADD_BOOL(account->board_delete_threads);
	WD_ACCOUNT_ADD_BOOL(account->board_edit_own_posts);
	WD_ACCOUNT_ADD_BOOL(account->board_edit_all_posts);

	return array;
}



static wi_array_t * wd_account_group_array(wd_account_t *account) {
	wi_array_t		*array;
	
	array = wi_array();
	
	wi_array_add_data(array, account->name);
	WD_ACCOUNT_ADD_BOOL(account->user_get_info);
	WD_ACCOUNT_ADD_BOOL(account->message_broadcast);
	WD_ACCOUNT_ADD_BOOL(account->board_add_posts);
	WD_ACCOUNT_ADD_BOOL(account->board_delete_posts);
	WD_ACCOUNT_ADD_BOOL(account->transfer_download_files);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_files);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_anywhere);
	WD_ACCOUNT_ADD_BOOL(account->file_create_directories);
	WD_ACCOUNT_ADD_BOOL(account->file_move_files);
	WD_ACCOUNT_ADD_BOOL(account->file_delete_files);
	WD_ACCOUNT_ADD_BOOL(account->file_access_all_dropboxes);
	WD_ACCOUNT_ADD_BOOL(account->account_create_accounts);
	WD_ACCOUNT_ADD_BOOL(account->account_edit_accounts);
	WD_ACCOUNT_ADD_BOOL(account->account_delete_accounts);
	WD_ACCOUNT_ADD_BOOL(account->account_raise_account_privileges);
	WD_ACCOUNT_ADD_BOOL(account->user_kick_users);
	WD_ACCOUNT_ADD_BOOL(account->user_ban_users);
	WD_ACCOUNT_ADD_BOOL(account->user_cannot_be_disconnected);
	WD_ACCOUNT_ADD_BOOL(account->transfer_download_speed_limit);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_speed_limit);
	WD_ACCOUNT_ADD_BOOL(account->transfer_download_limit);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_limit);
	WD_ACCOUNT_ADD_BOOL(account->chat_set_topic);
	wi_array_add_data(array, account->files);
	wi_array_add_data(array, wi_string_by_replacing_string_with_string(wi_date_rfc3339_string(account->creation_time),
		WI_STR(":"), WI_STR(";"), 0));
	wi_array_add_data(array, wi_string_by_replacing_string_with_string(wi_date_rfc3339_string(account->modification_time),
		WI_STR(":"), WI_STR(";"), 0));
	wi_array_add_data(array, account->edited_by);
	WD_ACCOUNT_ADD_BOOL(account->chat_create_chats);
	WD_ACCOUNT_ADD_BOOL(account->message_send_messages);
	WD_ACCOUNT_ADD_BOOL(account->board_read_boards);
	WD_ACCOUNT_ADD_BOOL(account->file_list_files);
	WD_ACCOUNT_ADD_BOOL(account->file_get_info);
	WD_ACCOUNT_ADD_BOOL(account->file_create_links);
	WD_ACCOUNT_ADD_BOOL(account->file_rename_files);
	WD_ACCOUNT_ADD_BOOL(account->file_set_type);
	WD_ACCOUNT_ADD_BOOL(account->file_set_comment);
	WD_ACCOUNT_ADD_BOOL(account->file_set_permissions);
	WD_ACCOUNT_ADD_BOOL(account->file_set_executable);
	WD_ACCOUNT_ADD_BOOL(account->file_recursive_list_depth_limit);
	WD_ACCOUNT_ADD_BOOL(account->transfer_upload_directories);
	WD_ACCOUNT_ADD_BOOL(account->account_change_password);
	WD_ACCOUNT_ADD_BOOL(account->account_list_accounts);
	WD_ACCOUNT_ADD_BOOL(account->account_read_accounts);
	WD_ACCOUNT_ADD_BOOL(account->user_cannot_set_nick);
	WD_ACCOUNT_ADD_BOOL(account->user_get_users);
	WD_ACCOUNT_ADD_BOOL(account->log_view_log);
	WD_ACCOUNT_ADD_BOOL(account->settings_get_settings);
	WD_ACCOUNT_ADD_BOOL(account->settings_set_settings);
	WD_ACCOUNT_ADD_BOOL(account->banlist_get_bans);
	WD_ACCOUNT_ADD_BOOL(account->banlist_add_bans);
	WD_ACCOUNT_ADD_BOOL(account->banlist_delete_bans);
	WD_ACCOUNT_ADD_BOOL(account->tracker_list_servers);
	WD_ACCOUNT_ADD_BOOL(account->tracker_register_servers);
	WD_ACCOUNT_ADD_BOOL(account->board_add_boards);
	WD_ACCOUNT_ADD_BOOL(account->board_move_boards);
	WD_ACCOUNT_ADD_BOOL(account->board_rename_boards);
	WD_ACCOUNT_ADD_BOOL(account->board_delete_boards);
	WD_ACCOUNT_ADD_BOOL(account->board_add_threads);
	WD_ACCOUNT_ADD_BOOL(account->board_move_threads);
	WD_ACCOUNT_ADD_BOOL(account->board_delete_threads);
	WD_ACCOUNT_ADD_BOOL(account->board_edit_own_posts);
	WD_ACCOUNT_ADD_BOOL(account->board_edit_all_posts);
	
	return array;
}




#pragma mark -

void wd_account_reply_user_account(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.user"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, account->name, WI_STR("wired.account.name"));
	wi_p7_message_set_string_for_name(reply, account->full_name, WI_STR("wired.account.full_name"));
	wi_p7_message_set_date_for_name(reply, account->creation_time, WI_STR("wired.account.creation_time"));
	wi_p7_message_set_date_for_name(reply, account->modification_time, WI_STR("wired.account.modification_time"));
	wi_p7_message_set_date_for_name(reply, account->login_time, WI_STR("wired.account.login_time"));
	wi_p7_message_set_string_for_name(reply, account->edited_by, WI_STR("wired.account.edited_by"));
	wi_p7_message_set_string_for_name(reply, account->password, WI_STR("wired.account.password"));
	wi_p7_message_set_string_for_name(reply, account->group, WI_STR("wired.account.group"));
	wi_p7_message_set_string_for_name(reply, account->files, WI_STR("wired.account.files"));
	wi_p7_message_set_list_for_name(reply, account->groups, WI_STR("wired.account.groups"));
	wd_account_write_privileges_to_message(account, reply);
	wd_user_reply_message(user, reply, message);
}



void wd_account_reply_group_account(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.group"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, account->name, WI_STR("wired.account.name"));
	wi_p7_message_set_date_for_name(reply, account->creation_time, WI_STR("wired.account.creation_time"));
	wi_p7_message_set_date_for_name(reply, account->modification_time, WI_STR("wired.account.modification_time"));
	wi_p7_message_set_string_for_name(reply, account->edited_by, WI_STR("wired.account.edited_by"));
	wi_p7_message_set_string_for_name(reply, account->files, WI_STR("wired.account.files"));
	wd_account_write_privileges_to_message(account, reply);
	wd_user_reply_message(user, reply, message);
}



wi_p7_message_t * wd_account_privileges_message(wd_account_t *account) {
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(WI_STR("wired.account.privileges"), wd_p7_spec);
	wd_account_write_privileges_to_message(account, message);
	
	return message;
}



#pragma mark -

wi_boolean_t wd_account_check_privileges(wd_account_t *account, wd_user_t *user) {
	wd_account_t	*user_account;
	
	user_account = wd_user_account(user);
	
	if(!user_account->account_raise_account_privileges) {
		if(!wi_string_has_prefix(account->files, user_account->files))
			return false;

		if(account->user_cannot_set_nick && !user_account->user_cannot_set_nick) return false;
		if(account->user_get_info && !user_account->user_get_info) return false;
		if(account->user_kick_users && !user_account->user_kick_users) return false;
		if(account->user_ban_users && !user_account->user_ban_users) return false;
		if(account->user_cannot_be_disconnected && !user_account->user_cannot_be_disconnected) return false;
		if(account->chat_set_topic && !user_account->chat_set_topic) return false;
		if(account->chat_create_chats && !user_account->chat_create_chats) return false;
		if(account->message_send_messages && !user_account->message_send_messages) return false;
		if(account->message_broadcast && !user_account->message_broadcast) return false;
		if(account->board_read_boards && !user_account->board_read_boards) return false;
		if(account->board_add_boards && !user_account->board_add_boards) return false;
		if(account->board_move_boards && !user_account->board_move_boards) return false;
		if(account->board_rename_boards && !user_account->board_rename_boards) return false;
		if(account->board_delete_boards && !user_account->board_delete_boards) return false;
		if(account->board_add_threads && !user_account->board_add_threads) return false;
		if(account->board_move_threads && !user_account->board_move_threads) return false;
		if(account->board_delete_threads && !user_account->board_delete_threads) return false;
		if(account->board_add_posts && !user_account->board_add_posts) return false;
		if(account->board_edit_own_posts && !user_account->board_edit_own_posts) return false;
		if(account->board_edit_all_posts && !user_account->board_edit_all_posts) return false;
		if(account->board_delete_posts && !user_account->board_delete_posts) return false;
		if(account->file_list_files && !user_account->file_list_files) return false;
		if(account->file_get_info && !user_account->file_get_info) return false;
		if(account->file_create_directories && !user_account->file_create_directories) return false;
		if(account->file_create_links && !user_account->file_create_links) return false;
		if(account->file_move_files && !user_account->file_move_files) return false;
		if(account->file_rename_files && !user_account->file_rename_files) return false;
		if(account->file_set_type && !user_account->file_set_type) return false;
		if(account->file_set_comment && !user_account->file_set_comment) return false;
		if(account->file_set_permissions && !user_account->file_set_permissions) return false;
		if(account->file_set_executable && !user_account->file_set_executable) return false;
		if(account->file_delete_files && !user_account->file_delete_files) return false;
		if(account->file_access_all_dropboxes && !user_account->file_access_all_dropboxes) return false;
		if(account->file_recursive_list_depth_limit > user_account->file_recursive_list_depth_limit) return false;
		if(account->transfer_download_files && !user_account->transfer_download_files) return false;
		if(account->transfer_upload_files && !user_account->transfer_upload_files) return false;
		if(account->transfer_upload_directories && !user_account->transfer_upload_directories) return false;
		if(account->transfer_upload_anywhere && !user_account->transfer_upload_anywhere) return false;
		if(account->transfer_download_limit > user_account->transfer_download_limit) return false;
		if(account->transfer_upload_limit > user_account->transfer_upload_limit) return false;
		if(account->transfer_download_speed_limit > user_account->transfer_download_speed_limit) return false;
		if(account->transfer_upload_speed_limit > user_account->transfer_upload_speed_limit) return false;
		if(account->account_change_password && !user_account->account_change_password) return false;
		if(account->account_list_accounts && !user_account->account_list_accounts) return false;
		if(account->account_read_accounts && !user_account->account_read_accounts) return false;
		if(account->account_create_accounts && !user_account->account_create_accounts) return false;
		if(account->account_edit_accounts && !user_account->account_edit_accounts) return false;
		if(account->account_delete_accounts && !user_account->account_delete_accounts) return false;
		if(account->account_raise_account_privileges && !user_account->account_raise_account_privileges) return false;
		if(account->user_get_users && !user_account->user_get_users) return false;
		if(account->log_view_log && !user_account->log_view_log) return false;
		if(account->settings_get_settings && !user_account->settings_get_settings) return false;
		if(account->settings_set_settings && !user_account->settings_set_settings) return false;
		if(account->banlist_get_bans && !user_account->banlist_get_bans) return false;
		if(account->banlist_add_bans && !user_account->banlist_add_bans) return false;
		if(account->banlist_delete_bans && !user_account->banlist_delete_bans) return false;
		if(account->tracker_list_servers && !user_account->tracker_list_servers) return false;
		if(account->tracker_register_servers && !user_account->tracker_register_servers) return false;
	}
	
	return true;
}
