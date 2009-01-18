/* $Id$ */

/*
 *  Copyright (c) 2008 Axel Andersson
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

#include <wired/wired.h>

#include "accounts.h"
#include "board.h"
#include "server.h"
#include "settings.h"

#define WD_BOARD_META_PATH						".wired"
#define WD_BOARD_META_PERMISSIONS_PATH			".wired/permissions"

#define WD_BOARD_PERMISSIONS_FIELD_SEPARATOR	"\34"


static wi_boolean_t								_wd_board_is_readable(wi_string_t *, wi_string_t *, wi_string_t *, wi_uinteger_t, wd_user_t *);
static wi_boolean_t								_wd_board_is_writable(wi_string_t *, wi_string_t *, wi_string_t *, wi_uinteger_t, wd_user_t *);
static wi_boolean_t								_wd_board_is_xable(wi_string_t *, wi_string_t *, wi_string_t *, wi_uinteger_t , wd_user_t *, wi_uinteger_t);
static wi_boolean_t								_wd_board_get_permissions(wi_string_t *, wi_string_t **, wi_string_t **, wi_uinteger_t *);
static wi_boolean_t								_wd_board_set_permissions(wi_string_t *, wi_string_t *, wi_string_t *, wi_uinteger_t, wd_user_t *, wi_p7_message_t *);
static void										_wd_board_broadcast_message(wi_string_t *, wi_p7_message_t *, wi_string_t *, wi_string_t *, wi_uinteger_t);
static wi_dictionary_t *						_wd_board_dictionary_with_post(wd_user_t *, wi_string_t *, wi_string_t *);
static wi_p7_message_t *						_wd_board_message_with_post(wi_string_t *, wi_string_t *, wi_uuid_t *, wi_uuid_t *, wi_dictionary_t *);
static wi_string_t *							_wd_board_board_path(wi_string_t *);
static wi_string_t *							_wd_board_thread_path(wi_string_t *board, wi_uuid_t *);
static wi_string_t *							_wd_board_post_path(wi_string_t *, wi_uuid_t *, wi_uuid_t *);
static wi_boolean_t								_wd_board_rename_board(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);


static wi_string_t								*_wd_board_path;
static wi_rwlock_t								*_wd_board_lock;



void wd_board_init(void) {
	_wd_board_path = WI_STR("board");
	_wd_board_lock = wi_rwlock_init(wi_rwlock_alloc());
}



#pragma mark -

void wd_board_reply_boards(wd_user_t *user, wi_p7_message_t *message) {
	wi_fsenumerator_t			*fsenumerator;
	wi_p7_message_t				*reply;
	wi_string_t					*path, *board, *owner, *group;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				mode, pathlength;
	
	pathlength = wi_string_length(_wd_board_path);

	wi_rwlock_rdlock(_wd_board_lock);

	fsenumerator = wi_fs_enumerator_at_path(_wd_board_path);
	
	if(fsenumerator) {
		while((status = wi_fsenumerator_get_next_path(fsenumerator, &path)) != WI_FSENUMERATOR_EOF) {
			if(status == WI_FSENUMERATOR_ERROR) {
				wi_log_err(WI_STR("Could not read board %@: %m"), path);

				continue;
			}
			
			if(wi_string_length(wi_string_path_extension(path)) > 0) {
				wi_fsenumerator_skip_descendents(fsenumerator);
				
				continue;
			}
			
			board = wi_string_substring_from_index(path, pathlength + 1);
			
			if(_wd_board_get_permissions(board, &owner, &group, &mode)) {
				if(_wd_board_is_readable(board, owner, group, mode, user)) {
					reply = wi_p7_message_with_name(WI_STR("wired.board.board_list"), wd_p7_spec);
					wi_p7_message_set_string_for_name(reply, board, WI_STR("wired.board.board"));
					wi_p7_message_set_string_for_name(reply, owner, WI_STR("wired.board.owner"));
					wi_p7_message_set_bool_for_name(reply, (mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.owner.read"));
					wi_p7_message_set_bool_for_name(reply, (mode & WD_BOARD_OWNER_WRITE), WI_STR("wired.board.owner.write"));
					wi_p7_message_set_string_for_name(reply, group, WI_STR("wired.board.group"));
					wi_p7_message_set_bool_for_name(reply, (mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.group.read"));
					wi_p7_message_set_bool_for_name(reply, (mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.group.write"));
					wi_p7_message_set_bool_for_name(reply, (mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.everyone.read"));
					wi_p7_message_set_bool_for_name(reply, (mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.everyone.write"));
					wd_user_reply_message(user, reply, message);
				}
			}
		}

		reply = wi_p7_message_with_name(WI_STR("wired.board.board_list.done"), wd_p7_spec);
		wd_user_reply_message(user, reply, message);
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), _wd_board_path);
		wd_user_reply_internal_error(user, message);
	}

	wi_rwlock_unlock(_wd_board_lock);
}



void wd_board_reply_posts(wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t		*instance;
	wi_fsenumerator_t			*fsenumerator;
	wi_p7_message_t				*reply;
	wi_array_t					*components;
	wi_uuid_t					*thread, *post;
	wi_string_t					*path, *board, *owner, *group;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				pathlength, count, mode;
	
	pathlength = wi_string_length(_wd_board_path);
	
	wi_rwlock_rdlock(_wd_board_lock);
	
	fsenumerator = wi_fs_enumerator_at_path(_wd_board_path);
	
	if(fsenumerator) {
		while((status = wi_fsenumerator_get_next_path(fsenumerator, &path)) != WI_FSENUMERATOR_EOF) {
			if(status == WI_FSENUMERATOR_ERROR) {
				wi_log_err(WI_STR("Could not read board %@: %m"), path);
				
				continue;
			}
			
			if(!wi_is_equal(wi_string_path_extension(path), WI_STR("WiredPost")))
				continue;
			
			instance = wi_plist_read_instance_from_file(path);
			
			if(instance && wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
				components	= wi_string_path_components(wi_string_substring_from_index(path, pathlength + 1));
				count		= wi_array_count(components);
				
				post		= wi_uuid_with_string(wi_string_by_deleting_path_extension(WI_ARRAY(components, count - 1)));
				thread		= wi_uuid_with_string(wi_string_by_deleting_path_extension(WI_ARRAY(components, count - 2)));
				board		= wi_array_components_joined_by_string(wi_array_subarray_with_range(components, wi_make_range(0, count - 2)), WI_STR("/"));
				
				if(_wd_board_get_permissions(board, &owner, &group, &mode)) {
					if(_wd_board_is_readable(board, owner, group, mode, user)) {
						reply = _wd_board_message_with_post(WI_STR("wired.board.post_list"), board, thread, post, instance);
						wd_user_reply_message(user, reply, message);
					}
				}
			}
		}

		reply = wi_p7_message_with_name(WI_STR("wired.board.post_list.done"), wd_p7_spec);
		wd_user_reply_message(user, reply, message);
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), _wd_board_path);
		wd_user_reply_internal_error(user, message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
}



#pragma mark -

void wd_board_add_board(wi_string_t *board, wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*path;
	wi_boolean_t		added = false;
	
	path = _wd_board_board_path(board);
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	if(!wi_fs_path_exists(path, NULL)) {
		if(wi_fs_create_directory(path, 0755)) {
			added = true;
			
			_wd_board_set_permissions(board, owner, group, mode, user, message);
		} else {
			wi_log_err(WI_STR("Could not create %@: %m"), path);
			wd_user_reply_internal_error(user, message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_exists"), message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
	
	if(added) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_added"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, owner, WI_STR("wired.board.owner"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.owner.read"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_OWNER_WRITE), WI_STR("wired.board.owner.write"));
		wi_p7_message_set_string_for_name(broadcast, group, WI_STR("wired.board.group"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_GROUP_READ), WI_STR("wired.board.group.read"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_GROUP_WRITE), WI_STR("wired.board.group.write"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_EVERYONE_READ), WI_STR("wired.board.everyone.read"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.everyone.write"));
		_wd_board_broadcast_message(board, broadcast, owner, group, mode);
	}
}



void wd_board_rename_board(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	
	if(_wd_board_rename_board(oldboard, newboard, user, message)) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_renamed"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	}
}



void wd_board_move_board(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	
	if(_wd_board_rename_board(oldboard, newboard, user, message)) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_moved"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	}
}



void wd_board_set_permissions(wi_string_t *board, wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	
	if(_wd_board_set_permissions(board, owner, group, mode, user, message)) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.permissions_changed"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, owner, WI_STR("wired.board.owner"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.owner.read"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_OWNER_WRITE), WI_STR("wired.board.owner.write"));
		wi_p7_message_set_string_for_name(broadcast, group, WI_STR("wired.board.group"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_GROUP_READ), WI_STR("wired.board.group.read"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_GROUP_WRITE), WI_STR("wired.board.group.write"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_EVERYONE_READ), WI_STR("wired.board.everyone.read"));
		wi_p7_message_set_bool_for_name(broadcast, (mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.everyone.write"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	}
}



void wd_board_delete_board(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*path;
	wi_boolean_t		deleted = false;
	
	path = _wd_board_board_path(board);
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	if(wi_fs_path_exists(path, NULL)) {
		if(wi_fs_delete_path(path)) {
			deleted = true;
		} else {
			wi_log_err(WI_STR("Could not delete %@: %m"), path);
			wd_user_reply_internal_error(user, message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
	
	if(deleted) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_deleted"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	}
}



#pragma mark -

wi_boolean_t wd_board_name_is_valid(wi_string_t *board) {
	if(wi_string_length(board) == 0)
		return false;
	
	if(wi_string_contains_string(board, WI_STR(".."), 0))
		return false;
	
	if(wi_string_has_prefix(board, WI_STR("/")))
		return false;
	
	return true;
}



#pragma mark -

static wi_boolean_t _wd_board_is_readable(wi_string_t *board, wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode, wd_user_t *user) {
	return _wd_board_is_xable(board, owner, group, mode, user, WD_BOARD_OWNER_READ | WD_BOARD_GROUP_READ | WD_BOARD_EVERYONE_READ);
}



static wi_boolean_t _wd_board_is_writable(wi_string_t *board, wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode, wd_user_t *user) {
	return _wd_board_is_xable(board, owner, group, mode, user, WD_BOARD_OWNER_WRITE | WD_BOARD_GROUP_WRITE | WD_BOARD_EVERYONE_WRITE);
}



static wi_boolean_t _wd_board_is_xable(wi_string_t *board, wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode, wd_user_t *user, wi_uinteger_t inmode) {
	wd_account_t	*account;
	
	if(mode & inmode)
		return true;
	
	account = wd_user_account(user);
	
	if(mode & inmode && wi_string_length(group) > 0) {
		if(wi_is_equal(group, account->group) || wi_array_contains_data(account->groups, group))
			return true;
	}
	
	if(mode & inmode && wi_string_length(owner) > 0) {
		if(wi_is_equal(owner, account->name))
			return true;
	}
	
	return false;
}



static wi_boolean_t _wd_board_get_permissions(wi_string_t *board, wi_string_t **owner, wi_string_t **group, wi_uinteger_t *mode) {
	wi_string_t		*path, *permissionspath, *string;
	wi_array_t		*array;
	wi_fs_stat_t	sb;

	path				= _wd_board_board_path(board);
	permissionspath		= wi_string_by_appending_path_component(path, WI_STR(WD_BOARD_META_PERMISSIONS_PATH));
	
	if(!wi_fs_stat_path(permissionspath, &sb)) {
		wi_log_warn(WI_STR("Could not open %@: %m"), permissionspath);
		
		return false;
	}
	
	if(sb.size > 128) {
		wi_log_warn(WI_STR("Could not read %@: Size is too large (%u"), permissionspath, sb.size);
		
		return false;
	}
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), permissionspath));
	
	if(!string) {
		wi_log_warn(WI_STR("Could not read %@: %m"), permissionspath);
		
		return false;
	}
	
	wi_string_delete_surrounding_whitespace(string);
	
	array = wi_string_components_separated_by_string(string, WI_STR(WD_BOARD_PERMISSIONS_FIELD_SEPARATOR));
	
	if(wi_array_count(array) != 3) {
		wi_log_info(WI_STR("Could not read %@: Contents is malformed (%@)"), permissionspath, string);
		
		return false;
	}
	
	*owner		= WI_ARRAY(array, 0);
	*group		= WI_ARRAY(array, 1);
	*mode		= wi_string_uint32(WI_ARRAY(array, 2));
	
	return true;
}



static wi_boolean_t _wd_board_set_permissions(wi_string_t *board, wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*path, *metapath, *permissionspath;
	wi_string_t		*string;
	
	path				= _wd_board_board_path(board);
	metapath			= wi_string_by_appending_path_component(path, WI_STR(WD_BOARD_META_PATH));
	permissionspath		= wi_string_by_appending_path_component(path, WI_STR(WD_BOARD_META_PERMISSIONS_PATH));
	
	if(!wi_fs_create_directory(metapath, 0777)) {
		if(wi_error_code() != EEXIST) {
			wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
			wd_user_reply_file_errno(user, message);
			
			return false;
		}
	}
	
	string = wi_string_with_format(WI_STR("%#@%s%#@%s%u\n"),
		owner,			WD_BOARD_PERMISSIONS_FIELD_SEPARATOR,
		group,			WD_BOARD_PERMISSIONS_FIELD_SEPARATOR,
		mode);
	
	if(!wi_string_write_to_file(string, permissionspath)) {
		wi_log_warn(WI_STR("Could not write to %@: %m"), permissionspath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	return true;
}



static void _wd_board_broadcast_message(wi_string_t *board, wi_p7_message_t *message, wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode) {
	wi_enumerator_t		*enumerator;
	wi_array_t			*users;
	wd_user_t			*user;
	
	users = wd_chat_users(wd_public_chat);
	
	wi_array_rdlock(users);

	enumerator = wi_array_data_enumerator(users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(user) == WD_USER_LOGGED_IN && _wd_board_is_readable(board, owner, group, mode, user))
			wd_user_send_message(user, message);
	}
	
	wi_array_unlock(users);
}



static wi_dictionary_t * _wd_board_dictionary_with_post(wd_user_t *user, wi_string_t *subject, wi_string_t *text) {
	return wi_dictionary_with_data_and_keys(
		wd_user_nick(user),		WI_STR("wired.user.nick"),
		wd_user_login(user),	WI_STR("wired.user.login"),
		wi_date(),				WI_STR("wired.board.post_date"),
		subject,				WI_STR("wired.board.subject"),
		text,					WI_STR("wired.board.text"),
		NULL);
}



static wi_p7_message_t * _wd_board_message_with_post(wi_string_t *name, wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wi_dictionary_t *dictionary) {
	wi_p7_message_t		*message;
	wi_date_t			*edit_date;
	
	message = wi_p7_message_with_name(name, wd_p7_spec);
	wi_p7_message_set_string_for_name(message, board, WI_STR("wired.board.board"));
	wi_p7_message_set_uuid_for_name(message, thread, WI_STR("wired.board.thread"));
	wi_p7_message_set_uuid_for_name(message, post, WI_STR("wired.board.post"));
	wi_p7_message_set_date_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.post_date")), WI_STR("wired.board.post_date"));
	
	edit_date = wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.edit_date"));
	
	if(edit_date)
		wi_p7_message_set_date_for_name(message, edit_date, WI_STR("wired.board.edit_date"));
	
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.nick")), WI_STR("wired.user.nick"));
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.login")), WI_STR("wired.user.login"));
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.subject")), WI_STR("wired.board.subject"));
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.text")), WI_STR("wired.board.text"));

	return message;
}



static wi_string_t * _wd_board_board_path(wi_string_t *board) {
	return wi_string_by_appending_path_component(_wd_board_path, board);
}



static wi_string_t * _wd_board_thread_path(wi_string_t *board, wi_uuid_t *thread) {
	wi_string_t		*path;
	
	path = _wd_board_board_path(board);
	
	wi_string_append_path_component(path, wi_uuid_string(thread));
	wi_string_append_path_extension(path, WI_STR("WiredThread"));
	
	return path;
}



static wi_string_t * _wd_board_post_path(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post) {
	wi_string_t		*path;
	
	path = _wd_board_board_path(board);
	
	wi_string_append_path_component(path, wi_uuid_string(thread));
	wi_string_append_path_extension(path, WI_STR("WiredThread"));
	wi_string_append_path_component(path, wi_uuid_string(post));
	wi_string_append_path_extension(path, WI_STR("WiredPost"));
	
	return path;
}



static wi_boolean_t _wd_board_rename_board(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*oldpath, *newpath;
	wi_boolean_t		renamed = false;
	
	oldpath = _wd_board_board_path(oldboard);
	newpath = _wd_board_board_path(newboard);
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	if(wi_fs_path_exists(oldpath, NULL)) {
		if(!wi_fs_path_exists(newpath, NULL)) {
			if(wi_fs_rename_path(oldpath, newpath)) {
				renamed = true;
			} else {
				wi_log_err(WI_STR("Could not rename %@ to %@: %m"), oldpath, newpath);
				wd_user_reply_internal_error(user, message);
			}
		} else {
			wd_user_reply_error(user, WI_STR("wired.error.board_exists"), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
	
	return renamed;
}



#pragma mark -

void wd_board_add_thread(wi_string_t *board, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_dictionary_t		*dictionary;
	wi_uuid_t			*thread, *post;
	wi_string_t			*path, *owner, *group;
	wi_uinteger_t		mode;
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	thread	= wi_uuid();
	path	= _wd_board_thread_path(board, thread);
	
	if(_wd_board_get_permissions(board, &owner, &group, &mode)) {
		if(_wd_board_is_writable(board, owner, group, mode, user)) {
			if(wi_fs_create_directory(path, 0755)) {
				dictionary	= _wd_board_dictionary_with_post(user, subject, text);
				post		= wi_uuid();
				path		= _wd_board_post_path(board, thread, post);
				
				if(wi_plist_write_instance_to_file(dictionary, path)) {
					broadcast = _wd_board_message_with_post(WI_STR("wired.board.post_added"), board, thread, post, dictionary);
					wd_chat_broadcast_message(wd_public_chat, broadcast);
				} else {
					wi_log_err(WI_STR("Could not create %@: %m"), path);
					wd_user_reply_internal_error(user, message);
				}
			} else {
				wi_log_err(WI_STR("Could not create %@: %m"), path);
				wd_user_reply_internal_error(user, message);
			}
		} else {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
}



void wd_board_move_thread(wi_string_t *oldboard, wi_uuid_t *thread, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*oldpath, *newpath, *oldowner, *oldgroup, *newowner, *newgroup;
	wi_uinteger_t		oldmode, newmode;
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	oldpath = _wd_board_thread_path(oldboard, thread);
	newpath = _wd_board_thread_path(newboard, thread);

	if(_wd_board_get_permissions(oldboard, &oldowner, &oldgroup, &oldmode) &&
	   _wd_board_get_permissions(newboard, &newowner, &newgroup, &newmode)) {
		if(_wd_board_is_writable(oldboard, oldowner, oldgroup, oldmode, user) &&
		   _wd_board_is_writable(newboard, newowner, newgroup, newmode, user)) {
			if(wi_fs_rename_path(oldpath, newpath)) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_moved"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
				wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wd_chat_broadcast_message(wd_public_chat, broadcast);
			} else {
				wi_log_err(WI_STR("Could not move %@ to %@: %m"), oldpath, newpath);
				wd_user_reply_internal_error(user, message);
			}
		} else {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
}



void wd_board_delete_thread(wi_string_t *board, wi_uuid_t *thread, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*path, *owner, *group;
	wi_uinteger_t		mode;
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	path = _wd_board_thread_path(board, thread);
	
	if(_wd_board_get_permissions(board, &owner, &group, &mode)) {
		if(_wd_board_is_writable(board, owner, group, mode, user)) {
			if(wi_fs_delete_path(path)) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_deleted"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wd_chat_broadcast_message(wd_public_chat, broadcast);
			} else {
				wi_log_err(WI_STR("Could not delete %@: %m"), path);
				wd_user_reply_internal_error(user, message);
			}
		} else {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
}



#pragma mark -

void wd_board_add_post(wi_string_t *board, wi_uuid_t *thread, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_dictionary_t		*dictionary;
	wi_uuid_t			*post;
	wi_string_t			*path;
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	post		= wi_uuid();
	path		= _wd_board_post_path(board, thread, post);
	dictionary	= _wd_board_dictionary_with_post(user, subject, text);
	
	if(wi_plist_write_instance_to_file(dictionary, path)) {
		broadcast = _wd_board_message_with_post(WI_STR("wired.board.post_added"), board, thread, post, dictionary);
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	} else {
		wi_log_err(WI_STR("Could not create %@: %m"), path);
		wd_user_reply_internal_error(user, message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
}



void wd_board_edit_post(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
	wi_p7_message_t			*broadcast;
	wi_date_t				*edit_date;
	wi_string_t				*path, *login, *owner, *group;
	wd_account_t			*account;
	wi_uinteger_t			mode;
	wi_boolean_t			edit = true;
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	path = _wd_board_post_path(board, thread, post);

	if(_wd_board_get_permissions(board, &owner, &group, &mode)) {
		if(_wd_board_is_writable(board, owner, group, mode, user)) {
			instance = wi_plist_read_instance_from_file(path);

			if(instance) {
				if(wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
					account = wd_user_account(user);
					
		//			if(!account->board_edit_all_posts && account->board_edit_own_posts) {
						login = wi_dictionary_data_for_key(instance, WI_STR("wired.user.login"));
						
						if(!wi_is_equal(login, wd_user_login(user))) {
							wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
							
							edit = false;
						}
		//			}
					
					if(edit) {
						edit_date = wi_date();
						
						wi_dictionary_set_data_for_key(instance, edit_date, WI_STR("wired.board.edit_date"));
						wi_dictionary_set_data_for_key(instance, subject, WI_STR("wired.board.subject"));
						wi_dictionary_set_data_for_key(instance, text, WI_STR("wired.board.text"));
						
						if(wi_plist_write_instance_to_file(instance, path)) {
							broadcast = wi_p7_message_with_name(WI_STR("wired.board.post_edited"), wd_p7_spec);
							wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
							wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
							wi_p7_message_set_uuid_for_name(broadcast, post, WI_STR("wired.board.post"));
							wi_p7_message_set_date_for_name(broadcast, edit_date, WI_STR("wired.board.edit_date"));
							wi_p7_message_set_string_for_name(broadcast, subject, WI_STR("wired.board.subject"));
							wi_p7_message_set_string_for_name(broadcast, text, WI_STR("wired.board.text"));
							wd_chat_broadcast_message(wd_public_chat, broadcast);
						} else {
							wi_log_err(WI_STR("Could not edit %@: %m"), path);
							wd_user_reply_internal_error(user, message);
						}
					}
				} else {
					wi_log_err(WI_STR("Could not edit %@: File is not a dictionary"), path);
					wd_user_reply_internal_error(user, message);
				}
			} else {
				wi_log_err(WI_STR("Could not open %@: %m"), path);
				wd_user_reply_internal_error(user, message);
			}
		} else {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
}



void wd_board_delete_post(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*path, *owner, *group;
	wi_uinteger_t		mode;
	
	wi_rwlock_wrlock(_wd_board_lock);
	
	path = _wd_board_post_path(board, thread, post);
	
	if(_wd_board_get_permissions(board, &owner, &group, &mode)) {
		if(_wd_board_is_writable(board, owner, group, mode, user)) {
			if(wi_fs_delete_path(path)) {
				(void) rmdir(wi_string_cstring(wi_string_by_deleting_last_path_component(path)));

				broadcast = wi_p7_message_with_name(WI_STR("wired.board.post_deleted"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wi_p7_message_set_uuid_for_name(broadcast, post, WI_STR("wired.board.post"));
				wd_chat_broadcast_message(wd_public_chat, broadcast);
			} else {
				wi_log_err(WI_STR("Could not delete %@: %m"), path);
				wd_user_reply_internal_error(user, message);
			}
		} else {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
	}
	
	wi_rwlock_unlock(_wd_board_lock);
}
